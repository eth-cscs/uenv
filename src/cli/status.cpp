// vim: ts=4 sts=4 sw=4 et

#include <algorithm>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <site/site.h>
#include <uenv/env.h>
#include <uenv/parse.h>
#include <uenv/repository.h>
#include <util/expected.h>
#include <util/strings.h>

#include "CLI/Validators.hpp"
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
    // status_cli->add_flag("--short", use_short);
    status_cli->add_option("--format", format, "desc")
        ->transform(CLI::IsMember({"short", "full", "views"}, CLI::ignore_case))
        ->default_val("full");

    status_cli->footer(status_footer);
}

int status([[maybe_unused]] const status_args& args,
           [[maybe_unused]] const global_settings& settings) {
    spdlog::info("uenv status");

    if (!in_uenv_session(settings.calling_environment)) {
        // --short is silent if no uenv is loaded
        if (!(args.format == "full")) {
            term::msg("there is no uenv loaded");
        }
        return 0;
    }

    // assume that the environment variables have been set, because this is
    // implied by the call to in_uenv_session passing
    std::string mount_desc =
        settings.calling_environment.get("UENV_MOUNT_LIST").value();
    std::string view_literal =
        settings.calling_environment.get("UENV_VIEW").value();

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

    const auto env = concretise_env(mount_desc, view_desc, settings.config.repo,
                                    settings.calling_environment);

    if (!env) {
        term::error("could not interpret environment: {}", env.error());
        return 1;
    }

    if (args.format == "full") {
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
                    std::string status =
                        loaded ? color::yellow(" (loaded)") : "";
                    term::msg("    {}{}: {}", color::cyan(name), status,
                              view.description);
                }
            }
        }
    } else if (args.format == "views") {
        // print `<name1>:<view1>|..|<nameN>:<viewN>`
        std::unordered_map<std::string, std::vector<std::string>> uenv_views;
        for (auto x : env->views) {
            uenv_views.try_emplace(x.uenv, std::vector<std::string>{});
            uenv_views[x.uenv].push_back(x.name);
        }
        auto name_views =
            uenv_views | std::views::transform([](const auto& pair) {
                const auto& [name, views] = pair;
                return fmt::format("{}:{}", name, fmt::join(views, ","));
            });
        term::msg("{}", fmt::join(name_views, "|"));
        return 0;
    } else if (args.format == "short") {
        // print `<name1>|..|<nameN>`
        term::msg(
            "{}",
            fmt::join(env->uenvs | std::views::transform([](const auto& pair) {
                          return fmt::format("{}", pair.first);
                      }),
                      "|"));

    } else {
        term::msg("Unknown argument to --format={}", args.format);
        return 1;
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
