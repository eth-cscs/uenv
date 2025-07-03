// vim: ts=4 sts=4 sw=4 et

#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <site/site.h>
#include <uenv/parse.h>
#include <uenv/print.h>
#include <uenv/repository.h>
#include <util/curl.h>
#include <util/expected.h>

#include "find.h"
#include "help.h"
#include "terminal.h"

namespace uenv {

std::string image_find_footer();

void image_find_args::add_cli(CLI::App& cli,
                              [[maybe_unused]] global_settings& settings) {
    auto* find_cli =
        cli.add_subcommand("find", "search for uenv that can be pulled");
    find_cli->add_option("uenv", uenv_description, "search term");
    find_cli->add_flag("--no-header", no_header,
                       "print only the matching records, with no header.");
    find_cli->add_flag("--json", json, "format output as JSON.");
    find_cli->add_flag("--build", build,
                       "invalid: replaced with 'build::' prefix on uenv label");
    find_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::image_find; });

    find_cli->footer(image_find_footer);
}

int image_find([[maybe_unused]] const image_find_args& args,
               [[maybe_unused]] const global_settings& settings) {
    if (args.build) {
        std::string descr = args.uenv_description.value_or("");
        term::error(
            "the --build flag has been removed.\nSpecify the build namespace "
            "as part of the uenv description, e.g.\n{}",
            color::yellow(fmt::format("uenv image find build::{}", descr)));
        return 1;
    }

    // find the search term that was provided by the user
    uenv_label label{};
    std::string nspace{site::default_namespace()};
    if (args.uenv_description) {
        if (const auto parse = parse_uenv_nslabel(*args.uenv_description)) {
            label = parse->label;
            if (parse->nspace) {
                nspace = *parse->nspace;
            }
        } else {
            term::error("invalid search term: {}", parse.error().message());
            return 1;
        }
    }
    label.system =
        site::get_system_name(label.system, settings.calling_environment);
    spdlog::info("image_find: {}::{}", nspace, label);

    auto store = site::registry_listing(nspace);
    if (!store) {
        term::error("unable to get a listing of the uenv", store.error());
        return 1;
    }

    // search db for matching records
    const auto result = store->query(label);
    if (!result) {
        term::error("invalid search term: {}", store.error());
        return 1;
    }

    // pass results to print
    print_record_set(*result, args.no_header, args.json);

    return 0;
}

std::string image_find_footer() {
    using enum help::block::admonition;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Search for uenv that are available to pull." },
        help::linebreak{},
        help::block{xmpl, "find all uenv"},
        help::block{code,   "uenv image find"},
        help::linebreak{},
        help::block{xmpl, "find all uenv with the name prgenv-gnu"},
        help::block{code,   "uenv image find prgenv-gnu"},
        help::linebreak{},
        help::block{xmpl, "find all uenv with the name prgenv-gnu and version 24.7"},
        help::block{code,   "uenv image find prgenv-gnu/24.7"},
        help::linebreak{},
        help::block{xmpl, "find all uenv with the name prgenv-gnu, version 24.7 and release v2"},
        help::block{code, "uenv image find prgenv-gnu/24.7:v2"},
        help::linebreak{},
        help::block{xmpl, "use the @ symbol to specify a target system name"},
        help::block{code,   "uenv image find prgenv-gnu@todi"},
        help::block{none, "this feature is useful when using images that were built for a different system",
                          "than the one you are currently working on."},
        help::linebreak{},
        help::block{xmpl, "use the @ symbol to specify a target system name"},
        help::block{code,   "uenv image find prgenv-gnu@todi"},
        help::block{none, "this feature is useful when using images that were built for a different system",
                          "than the one you are currently working on."},
        help::linebreak{},
        help::block{xmpl, "use the % symbol to specify a target microarchitecture (uarch)"},
        help::block{code,   "uenv image find prgenv-gnu%gh200"},
        help::block{none, "this feature is useful on a system with multiple uarch."},
        help::linebreak{},
        help::block{xmpl, "list any uenv with a concrete sha256 checksum"},
        help::block{code,   "uenv image find 510094ddb3484e305cb8118e21cbb9c94e9aff2004f0d6499763f42bdafccfb5"},
        help::linebreak{},
        help::block{note, "more than one uenv might be listed if there are two uenv that refer",
                          "to the same underlying uenv sha256."},
        help::linebreak{},
        help::block{xmpl, "search for uenv by id (id is the first 16 characters of the sha256)"},
        help::block{code,   "uenv image find 510094ddb3484e30"},
        help::linebreak{},
        help::block{xmpl, "search for uenv in the service namespace"},
        help::block{code,   "uenv image find service::           # all uenv"},
        help::block{code,   "uenv image find service::prgenv-gnu # match a name"},
        help::block{code,   "uenv image find service::%gh200     # built for gh200"},
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace uenv
