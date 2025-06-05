// vim: ts=4 sts=4 sw=4 et

#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <site/site.h>
#include <uenv/parse.h>
#include <uenv/repository.h>
#include <util/expected.h>

#include "help.h"
#include "inspect.h"
#include "terminal.h"

namespace uenv {

std::string image_inspect_footer();

void image_inspect_args::add_cli(CLI::App& cli,
                                 [[maybe_unused]] global_settings& settings) {
    auto* inspect_cli =
        cli.add_subcommand("inspect", "print information about a uenv.");
    inspect_cli->add_option("--format", format, "the format string.");
    inspect_cli->add_option("uenv", label, "the uenv to inspect.")->required();
    inspect_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::image_inspect; });

    inspect_cli->footer(image_inspect_footer);
}

int image_inspect([[maybe_unused]] const image_inspect_args& args,
                  [[maybe_unused]] const global_settings& settings) {
    spdlog::info("image inspect {}", args.label);

    // get the repo and handle errors if it does not exist
    if (!settings.config.repo) {
        term::error("a repo needs to be provided either using the --repo flag "
                    "in the config file");
        return 1;
    }

    // open the repo
    auto store = uenv::open_repository(*settings.config.repo);
    if (!store) {
        term::error("unable to open repo: {}", store.error());
        return 1;
    }

    // find the search term that was provided by the user
    uenv_label label{};
    if (const auto parse = parse_uenv_label(args.label)) {
        label = *parse;
    } else {
        term::error("invalid search term: {}", parse.error().message());
        return 1;
    }

    // set label->system to the current cluster name if it has not
    // already been set.
    label.system =
        site::get_system_name(label.system, settings.calling_environment);

    // query the repo
    const auto result = store->query(label);
    if (!result) {
        term::error("invalid search term: {}", store.error());
        return 1;
    }

    if (result->empty()) {
        term::error("no matching uenv");
        return 0;
    }

    if (result->size() > 1) {
        term::error("more than one uenv match the search criteria '{}'", label);
        fmt::print("\n");
        for (const auto& r : *result) {
            fmt::print("{}  {}/{}:{}\n", r.id, r.name, r.version, r.tag);
        }

        return 1;
    }

    const auto r = *result->begin();
    auto paths = store->uenv_paths(r.sha);

    try {
        std::string meta_path =
            std::filesystem::exists(paths.meta) ? paths.meta.string() : "none";
        spdlog::debug("inspect format string: '{}'", args.format);
        // clang-format off
        auto msg = fmt::format(fmt::runtime(args.format),
                        fmt::arg("name", r.name),
                        fmt::arg("version", r.version),
                        fmt::arg("tag", r.tag),
                        fmt::arg("id", r.id),
                        fmt::arg("sha256", r.sha),
                        fmt::arg("date", r.date),
                        fmt::arg("system", r.system),
                        fmt::arg("uarch", r.uarch),
                        fmt::arg("path", paths.store.string()),
                        fmt::arg("sqfs", paths.squashfs.string()),
                        fmt::arg("meta", meta_path)
                   );
        // clang-format on
        fmt::print("{}\n", msg);
    } catch (fmt::format_error& e) {
        spdlog::error("error reading meta data: {}", e.what());
        term::error("unable to read uenv meta data", e.what());
        return 1;
    }

    return 0;
}

std::string image_inspect_footer() {
    using enum help::block::admonition;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Display detailed information about a uenv." },
        help::linebreak{},
        help::block{xmpl, "get information about a uenv using a label, sha or id"},
        help::block{code,   "uenv image inspect prgenv-gnu/24.7:v1"},
        help::block{code,   "uenv image inspect 3313739553fe6553f789a35325eb6954a37a7b85cdeab943d0878a05edaac998"},
        help::block{code,   "uenv image inspect 3313739553fe6553"},
        help::linebreak{},
        help::block{note, "the spec must uniquely identify the uenv."},
        help::block{none, "If more than one uenv match the spec, an error message is printed."},
        help::linebreak{},
        help::block{xmpl, "print the path of a uenv"},
        help::block{code,   "uenv image inspect --format='{path}' prgenv-gnu/24.2:v1"},
        help::linebreak{},
        help::block{xmpl, "print the name and tag of a uenv un the name:tag format"},
        help::block{code,   "uenv image inspect --format='{name}:{tag}' prgenv-gnu/24.2:v1"},
        help::linebreak{},
        help::block{xmpl, "print the location of the squashfs file of an image"},
        help::block{code, "uenv image inspect --format='{sqfs}' prgenv-gnu/24.2:v1"},
        help::linebreak{},
        help::block{info, "including name, tag and sqfs, the following variables can be printed in a"},
        help::block{none, "format string passed to the --format option:"},
        help::block{none, "    name:    name"},
        help::block{none, "    version: version"},
        help::block{none, "    tag:     tag"},
        help::block{none, "    id:      the 16 digit image id"},
        help::block{none, "    sha256:  the unique sha256 hash of the uenv"},
        help::block{none, "    date:    date that the uenv was created"},
        help::block{none, "    system:  the system that the uenv was built for"},
        help::block{none, "    uarch:   the micro-architecture that the uenv was built for"},
        help::block{none, "    path:    absolute path where the uenv is stored"},
        help::block{none, "    sqfs:    absolute path of the squashfs file"},
        help::block{none, "    meta:    absolute path of the image meta data, set to"},
        help::block{none, "             \"none\" if there is no meta data path"},
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace uenv
