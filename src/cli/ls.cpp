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

#include "help.h"
#include "ls.h"

namespace uenv {

std::string image_ls_footer();

void image_ls_args::add_cli(CLI::App& cli,
                            [[maybe_unused]] global_settings& settings) {
    auto* ls_cli =
        cli.add_subcommand("ls", "search for uenv that are available to run");
    ls_cli->add_option("uenv", uenv_description,
                       "comma separated list of uenv to mount.");
    ls_cli->add_flag("--no-header", no_header,
                     "print only the matching records, with no header.");
    ls_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::image_ls; });

    ls_cli->footer(image_ls_footer);
}

int image_ls(const image_ls_args& args, const global_settings& settings) {
    spdlog::info("image ls {}",
                 args.uenv_description ? *args.uenv_description : "none");

    // get the repo and handle errors if it does not exist
    if (!settings.repo) {
        spdlog::error(
            "a repo needs to be provided either using the --repo flag "
            "or by setting the UENV_REPO_PATH environment variable");
        return 1;
    }

    // open the repo
    auto store = uenv::open_repository(*settings.repo);
    if (!store) {
        spdlog::error("unable to open repo: {}", store.error());
        return 1;
    }

    // find the search term that was provided by the user
    uenv_label label{};
    if (args.uenv_description) {
        if (const auto parse = parse_uenv_label(*args.uenv_description)) {
            label = *parse;
        } else {
            spdlog::error("invalid search term: {}", parse.error().message());
            return 1;
        }
    }

    // set label->system to the current cluster name if it has not
    // already been set.
    label.system = cscs::get_system_name(label.system);

    // query the repo
    const auto result = store->query(label);
    if (!result) {
        spdlog::error("invalid search term: {}", store.error());
        return 1;
    }

    if (result->empty()) {
        if (!args.no_header) {
            fmt::println("no matching uenv");
        }
        return 0;
    }

    // print the results
    std::size_t w_name = std::string_view("uenv").size();
    std::size_t w_sys = std::string_view("system").size();
    std::size_t w_arch = std::string_view("arch").size();

    for (auto& r : *result) {
        w_name = std::max(
            w_name, fmt::format("{}/{}:{}", r.name, r.version, r.tag).size());
        w_sys = std::max(w_sys, r.system.size());
        w_arch = std::max(w_arch, r.uarch.size());
    }
    ++w_name;
    ++w_sys;
    ++w_arch;
    if (!args.no_header) {
        fmt::println("{:<{}}{:<{}}{:<{}}{:<17}{:<10}", "uenv", w_name, "arch",
                     w_arch, "system", w_sys, "id", "date");
    }
    for (auto& r : *result) {
        auto name = fmt::format("{}/{}:{}", r.name, r.version, r.tag);
        fmt::println("{:<{}}{:<{}}{:<{}}{:<17}{:s}", name, w_name, r.uarch,
                     w_arch, r.system, w_sys, r.id.string(), r.date);
    }

    return 0;
}

std::string image_ls_footer() {
    using enum help::block::admonition;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Search for uenv that are available to run." },
        help::linebreak{},
        help::block{xmpl, "list all uenv"},
        help::block{code,   "uenv image ls"},
        help::linebreak{},
        help::block{xmpl, "list all uenv"},
        help::block{code,   "uenv image ls"},
        help::linebreak{},
        help::block{xmpl, "list all uenv with the name prgenv-gnu"},
        help::block{code,   "uenv image ls prgenv-gnu"},
        help::linebreak{},
        help::block{xmpl, "list all uenv with the name prgenv-gnu and version 24.7"},
        help::block{code,   "uenv image ls prgenv-gnu/24.7"},
        help::linebreak{},
        help::block{xmpl, "list all uenv with the name prgenv-gnu, version 24.7 and release v2"},
        help::block{code, "uenv image ls prgenv-gnu/24.7:v2"},
        help::linebreak{},
        help::block{xmpl, "use the @ symbol to specify a target system name"},
        help::block{code,   "uenv image ls prgenv-gnu@todi"},
        help::block{none, "this feature is useful when using images that were built for a different system",
                          "than the one you are currently working on."},
        help::linebreak{},
        help::block{xmpl, "use the @ symbol to specify a target system name"},
        help::block{code,   "uenv image ls prgenv-gnu@todi"},
        help::block{none, "this feature is useful when using images that were built for a different system",
                          "than the one you are currently working on."},
        help::linebreak{},
        help::block{xmpl, "use the % symbol to specify a target microarchitecture (uarch)"},
        help::block{code,   "uenv image ls prgenv-gnu%gh200"},
        help::block{none, "this feature is useful on a system with multiple uarch."},
        help::linebreak{},
        help::block{xmpl, "list any uenv with a concrete sha256 checksum"},
        help::block{code,   "uenv image ls 510094ddb3484e305cb8118e21cbb9c94e9aff2004f0d6499763f42" "bdafccfb5"},
        help::linebreak{},
        help::block{note, "more than one uenv might be listed if there are two uenv that refer",
                          "to the same underlying uenv sha256."},
        help::linebreak{},
        help::block{xmpl, "search for uenv by id (id is the first 16 characters of the sha256):"},
        help::block{code,   "uenv image ls 510094ddb3484e30"},
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace uenv
