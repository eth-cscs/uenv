// vim: ts=4 sts=4 sw=4 et

#include <algorithm>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <vector>

#include <CLI/Validators.hpp>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/env.h>
#include <uenv/parse.h>
#include <uenv/telemetry.h>
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
    status_cli->add_flag("--error-if-unset", error_if_unset,
                         "return a nonzero error code if no uenv is loaded");
    status_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::status; });
    status_cli
        ->add_option("--format", format, "one of {full (default), name, views}")
        ->transform(CLI::CheckedTransformer(
            std::unordered_map<std::string, status_format>{
                {"short", status_format::name},
                {"full", status_format::full},
                {"views", status_format::views}}));

    status_cli->footer(status_footer);
}

int status([[maybe_unused]] const status_args& args,
           [[maybe_unused]] const global_settings& settings) {
    spdlog::info("uenv status");
    using enum status_format;

    if (!in_uenv_session(settings.calling_environment)) {
        // only print output in full mode
        if (args.format == full) {
            term::msg("there is no uenv loaded");
        }
        return args.error_if_unset ? 1 : 0;
    }

    auto telemetry = uenv::telemetry_from_env(settings.calling_environment);

    if (!telemetry) {
        term::error("unable to find uenv status");
        return 1;
    }

    if (args.format == full) {
        for (auto& E : *telemetry) {
            term::msg(
                "uenv  {}\n  mount  {}\n  views  [{}]", color::yellow(E.name),
                color::cyan(E.mount),
                fmt::join(E.views | std::views::transform([](const auto& v) {
                              return color::cyan(v);
                          }),
                          ", "));
        }
    } else if (args.format == views) {
        term::msg(
            "{}",
            fmt::join(*telemetry | std::views::transform([](const auto& u) {
                if (u.views.empty()) {
                    return fmt::format("{}", color::yellow(u.name));
                }
                return fmt::format(
                    "{}[{}]", color::yellow(u.name),
                    fmt::join(u.views |
                                  std::views::transform([](const auto& v) {
                                      return color::cyan(v);
                                  }),
                              ","));
            }),
                      ","));
    } else if (args.format == name) {
        term::msg("{}", fmt::join(*telemetry |
                                      std::views::transform(
                                          [](const auto& u) { return u.name; }),
                                  ","));
    }

    return 0;
}

std::string status_footer() {
    using enum help::block::admonition;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Display information about the current uenv environment." },
        help::linebreak{},
        help::block{xmpl, "get status:"},
        help::block{code,   "uenv status"},
        help::linebreak{},
        help::block{note, "if no uenv is loaded, the message 'there is no no uenv loaded' will be printed"},
        help::block{xmpl, "control the formatting of the output:"},
        help::block{code,   "uenv status --format=full  # verbose output (default)"},
        help::block{code,   "uenv status --format=short # comma-separated list of uenv name"},
        help::block{code,   "uenv status --format=views # comma-separated list of uenv:[views]"},
        help::linebreak{},
        help::block{note, "the 'name' and 'views' options print no output if no uenv is running.",
                          "These options are useful for scripting and setting command line prompts."},
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace uenv
