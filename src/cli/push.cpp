// vim: ts=4 sts=4 sw=4 et

#include <csignal>
#include <filesystem>
#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <site/site.h>
#include <uenv/oras.h>
#include <uenv/parse.h>
#include <uenv/print.h>
#include <uenv/repository.h>
#include <util/curl.h>
#include <util/expected.h>
#include <util/fs.h>
#include <util/signal.h>

#include "help.h"
#include "push.h"
#include "terminal.h"

namespace uenv {

std::string image_push_footer();

void image_push_args::add_cli(CLI::App& cli,
                              [[maybe_unused]] global_settings& settings) {
    auto* push_cli = cli.add_subcommand("push", "push a uenv to a registry");
    push_cli
        ->add_option("source", source,
                     "the local uenv to push, either name/version:tag, sha256, "
                     "id, or the path of a SquashFS file")
        ->required();
    push_cli
        ->add_option("dest", dest,
                     "the destination in the full "
                     "namespace::name/version:tag@system%uarch form")
        ->required();
    push_cli->add_option(
        "--token", token,
        "a path that contains a TOKEN file for accessing the registry");
    push_cli->add_option("--username", username,
                         "user name for the registry (by default the username "
                         "on the system will be used).");
    push_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::image_push; });

    push_cli->footer(image_push_footer);
}

int image_push([[maybe_unused]] const image_push_args& args,
               [[maybe_unused]] const global_settings& settings) {
    // namespace fs = std::filesystem;

    std::optional<uenv::oras::credentials> credentials;
    if (auto c = site::get_credentials(args.username, args.token)) {
        credentials = *c;
    } else {
        term::error("{}", c.error());
        return 1;
    }

    // TODO

    return 0;
} // namespace uenv

std::string image_push_footer() {
    using enum help::block::admonition;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Push a uenv to a registry." },
        help::linebreak{},
        help::linebreak{},
        help::block{xmpl, "push a uenv from your local repository"},
        help::block{code,   "uenv image push prgenv-gnu/24.11:v3 prgenv-gnu/24.11:v3%gh200@daint"},
        help::linebreak{},
        help::block{xmpl, "push a uenv from a SquashFS file on the local filesystem"},
        help::block{code,   "uenv image push ./store.squashfs prgenv-gnu/24.11:v3%gh200@daint"},
        help::linebreak{},
        help::block{xmpl, "use a token for the registry"},
        help::block{code,   "uenv image push --token=/opt/cscs/uenv/tokens/vasp6 \\"},
        help::block{code,   "                ./store.squashfs prgenv-gnu/24.11:v3%gh200@daint"},
        help::block{note, "this is required if you have not configured ~/.docker/config.json" },
        help::block{none, "with the token for the registry." },
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace uenv
