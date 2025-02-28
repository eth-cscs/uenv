// vim: ts=4 sts=4 sw=4 et

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <site/site.h>
#include <uenv/parse.h>
#include <uenv/repository.h>
#include <util/expected.h>
#include <util/strings.h>

#include "help.h"
#include "status.h"
#include "terminal.h"

namespace uenv {

std::string status_footer();

void status_args::add_cli(CLI::App& cli,
                          [[maybe_unused]] global_settings& settings) {
    auto* status_cli = cli.add_subcommand(
        "status", "print information about the currently loaded uenv");
    status_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::status; });

    status_cli->footer(status_footer);
}

int status([[maybe_unused]] const status_args& args,
           [[maybe_unused]] const global_settings& settings) {
    spdlog::info("uenv status");

    if (!in_uenv_session()) {
        term::msg("there is no uenv loaded");
        return 0;
    }

    std::string mount_desc = std::getenv("UENV_MOUNT_LIST");
    std::string view_literal = std::getenv("UENV_VIEW");

    // the UENV_VIEW environment variable is a comma-separated list of the form
    //   mount:uenv-name:view-name
    // concretise_uenv requires a comma-separated list of the form
    //   uenv-name:view-name
    std::string view_string = "";
    for (auto view : util::split(view_literal, ',', true)) {
        auto terms = util::split(view, ':', true);
        if (terms.size() != 3) {
            // just skip incorrectly formatted view description
            spdlog::warn(
                "incorrectly formatted view description in UENV_VIEW: '{}'",
                view);
            continue;
        }
        view_string += fmt::format("{}:{},", terms[1], terms[2]);
    }
    std::optional<std::string> view_desc;
    if (!view_string.empty()) {
        view_desc = view_string;
    }
    spdlog::debug("derived view description from UENV_VIEW {}", view_desc);

    const auto env =
        concretise_env(mount_desc, view_desc, settings.config.repo);

    if (!env) {
        term::error("could not interpret environment: {}", env.error());
        return 1;
    }

    for (auto& [name, E] : env->uenvs) {
        term::msg("{}:{}", color::cyan(name), color::white(E.mount_path));
        if (E.description) {
            term::msg("  {}", *E.description);
        }
        if (!E.views.empty()) {
            term::msg("  {}:", color::white("views"));
            for (auto& [name, view] : E.views) {
                const bool loaded =
                    std::ranges::find_if(
                        env->views, [name, uenv = E.name](auto& p) {
                            return p.name == name && p.uenv == uenv;
                        }) != env->views.end();
                std::string status = loaded ? color::yellow(" (loaded)") : "";
                term::msg("    {}{}: {}", color::cyan(name), status,
                          view.description);
            }
        }
    }

    return 0;
}

std::string status_footer() {
    using enum help::block::admonition;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Disply information about the current uenv environment." },
        help::linebreak{},
        help::block{xmpl, "get status:"},
        help::block{code,   "uenv status"},
        help::linebreak{},
        help::block{note, "if no uenv is loaded, the message 'there is no no uenv loaded' will be printed"},
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace uenv
