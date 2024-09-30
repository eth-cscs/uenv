// vim: ts=4 sts=4 sw=4 et

#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/cscs.h>
#include <uenv/parse.h>
#include <uenv/repository.h>
#include <util/expected.h>

#include "add_remove.h"
#include "help.h"

namespace uenv {

std::string image_add_footer();
std::string image_remove_footer();

void image_add_args::add_cli(CLI::App& cli,
                             [[maybe_unused]] global_settings& settings) {
    auto* add_cli = cli.add_subcommand("add", "manage and query uenv images");
    add_cli->add_option("uenv", uenv_description, "the uenv to add.");
    add_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::image_add; });

    add_cli->footer(image_add_footer);
}

void image_remove_args::add_cli(CLI::App& cli,
                                [[maybe_unused]] global_settings& settings) {
    auto* remove_cli =
        cli.add_subcommand("remove", "manage and query uenv images");
    remove_cli->add_option("uenv", uenv_description, "the uenv to remove.");
    remove_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::image_remove; });

    remove_cli->footer(image_remove_footer);
}

int image_add(const image_add_args& args, const global_settings& settings) {
    return 0;
}

int image_remove(const image_remove_args& args,
                 const global_settings& settings) {
    return 0;
}

std::string image_add_footer() {
    using enum help::block::admonition;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Add a uenv image to a repository." },
        help::linebreak{},
        help::block{xmpl, "add an image, providing the full label"},
        help::block{code,   "uenv image add myenv/24.7:v1 ./store.squashfs"},
        help::linebreak{},
        help::block{xmpl, "infer the name from the meta data"},
        help::block{code,   "uenv image add --version=24.7 --tag=v1 ./store.squashfs"},
        help::linebreak{},
        help::block{xmpl, "infer the name from the meta data"},
        help::block{code,   "uenv image add --version=24.7 --tag=v1 --uarch=a100 ./store.squashfs"},
        help::linebreak{},
        help::block{xmpl, "add cluster name"},
        help::block{code,   "uenv image add --version=24.7 --tag=v1 --uarch=a100 --system=daint ./store.squashfs"},
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

std::string image_remove_footer() {
    using enum help::block::admonition;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Remove a uenv image from a repository." },
        help::linebreak{},
        help::block{xmpl, "by label"},
        help::block{code,   "uenv image remove prgenv-gnu/24.7:v1"},
        help::linebreak{},
        help::block{xmpl, "by label"},
        help::block{code,   "uenv image remove prgenv-gnu"},
        help::block{none, "should this delete all images that match?"},
        help::linebreak{},
        help::block{xmpl, "by sha"},
        help::block{code,   "uenv image remove abcd1234abcd1234abcd1234abcd1234"},
        help::linebreak{},
        help::block{xmpl, "by id"},
        help::block{code,   "uenv image remove abcd1234"},
        help::linebreak{},
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace uenv
