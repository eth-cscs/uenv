// vim: ts=4 sts=4 sw=4 et

#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/parse.h>
#include <uenv/print.h>
#include <uenv/repository.h>
#include <util/expected.h>

#include "help.h"
#include "ls.h"
#include "terminal.h"
#include "uenv.h"

namespace uenv {

std::string image_ls_footer();

void image_ls_args::add_cli(CLI::App& cli,
                            [[maybe_unused]] global_settings& settings) {
    auto* ls_cli =
        cli.add_subcommand("ls", "search for uenv that are available to run");
    ls_cli->add_option("uenv", uenv_description, "search term");
    ls_cli->add_flag("--no-header", no_header,
                     "print only the matching records, with no header.");
    ls_cli->add_flag("--json", json,
                     "format output as JSON (incompatible with --list).");
    ls_cli->add_option(
        "--format", format,
        "optional format specification (incompatible with --json).");
    ls_cli->add_flag("--no-partials", no_partials,
                     "do not match partial names when searching.");
    ls_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::image_ls; });

    ls_cli->footer(image_ls_footer);
}

int image_ls(const image_ls_args& args, const global_settings& settings) {
    auto format =
        get_record_set_format(args.no_header, args.json, (bool)args.format);
    if (!format) {
        term::error("{}", format.error());
        return 1;
    }

    // print a warning if there are no repos
    // skip the warning if outputing json format so that we don't pollute
    if (settings.config.repos.empty() && *format != record_set_format::json) {
        term::warn("{}", messages::no_repos());
    }

    // parse the search term once, before opening any repos
    uenv_label label{};
    if (args.uenv_description) {
        if (const auto parse = parse_uenv_label(*args.uenv_description)) {
            label = *parse;
        } else {
            term::error("invalid search term: {}", parse.error().message());
            return 1;
        }
    }
    label = apply_system(label, settings.config.system_name);

    // query each repo in priority order
    std::vector<repo_record_set> results;
    for (const auto& repo : settings.config.repos) {
        auto store = uenv::open_repository(repo.path);
        if (!store) {
            term::warn("unable to open repo {}: {}", repo.name, store.error());
            continue;
        }
        auto query = store->query(label, !args.no_partials);
        if (!query) {
            term::warn("unable to query repo {}: {}", repo.name, query.error());
            continue;
        }
        results.push_back({repo, std::move(query.value())});
    }

    print_repo_record_sets(
        results, format.value(),
        format.value() == record_set_format::list ? args.format.value() : "");

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
        help::block{code,   "uenv image ls 510094ddb3484e305cb8118e21cbb9c94e9aff2004f0d6499763f42bdafccfb5"},
        help::linebreak{},
        help::block{note, "more than one uenv might be listed if there are two uenv that refer",
                          "to the same underlying uenv sha256."},
        help::linebreak{},
        help::block{xmpl, "search for uenv by id (id is the first 16 characters of the sha256):"},
        help::block{code,   "uenv image ls 510094ddb3484e30"},
        help::linebreak{},
        help::block{xmpl, "list all uenv on any target system"},
        help::block{code,   "uenv image ls @*"},

        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace uenv
