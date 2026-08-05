// vim: ts=4 sts=4 sw=4 et

#include <CLI/CLI.hpp>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <site/site.h>

#include "config.h"
#include "help.h"

namespace uenv {

std::string configure_footer();

void configure_args::add_cli(CLI::App& cli, global_settings& settings) {
    auto* configure_cli =
        cli.add_subcommand("config", "print uenv tool configuration");
    configure_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::configure; });

    configure_cli->footer(configure_footer);
}

int configure([[maybe_unused]] const configure_args& args,
              const global_settings& settings) {
    spdlog::info("uenv config");

    const auto& config = settings.config;

    fmt::println("configuration-files:");
    const auto system_path = system_config_path();
    fmt::println("  system:      {}",
                 (system_path ? color::green(system_path->string())
                              : color::red("none")));
    const auto user_path = user_config_path(settings.calling_environment);
    fmt::println(
        "  user:        {}",
        (user_path ? color::green(user_path->string()) : color::red("none")));

    // print the global configuration
    fmt::println("uenv-configuration:");
    fmt::println("  system:      {}",
                 (config.system_name ? color::green(config.system_name.value())
                                     : color::red("none")));
    if (config.repos.empty()) {
        fmt::println("  repos:       {}", color::red("none"));
    } else {
        fmt::print("  repos:       ");
        bool first = true;
        for (auto& r : config.repos) {
            if (first) {
                fmt::println("{}:{}", color::yellow(r.name),
                             color::cyan(r.path));
                first = false;
            } else {
                fmt::println("               {}:{}", color::yellow(r.name),
                             color::cyan(r.path));
            }
        }
    }
    if (config.registry) {
        fmt::println("  registry:    {}/{}",
                     color::yellow(config.registry->url.string()),
                     color::cyan(config.registry->default_namespace));
        fmt::println(
            "  artifactory: {}",
            config.registry->artifactory_url
                ? color::green(config.registry->artifactory_url->string())
                : color::red("none"));
        // unlike the other registry fields, an unset listing_url is not "none":
        // the built-in CSCS endpoint is used. Print what will actually be
        // queried.
        fmt::println(
            "  listing:     {}",
            color::green(config.registry->listing_url
                             ? config.registry->listing_url->string()
                             : std::string{site::default_listing_url}));
    } else {
        fmt::println("  registry:    {}", color::red("none"));
    }
    fmt::println("  color:       {}",
                 (config.color ? color::green("on") : color::red("off")));
    fmt::println("  elastic:     {}",
                 (config.elastic_config
                      ? color::green(config.elastic_config.value())
                      : color::red("none")));

    return 0;
}

std::string configure_footer() {
    using enum help::block::admonition;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Display information about the configuration of uenv." },
        help::linebreak{},
        help::block{xmpl, "get configuration:"},
        help::block{code,   "uenv config"},
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace uenv
