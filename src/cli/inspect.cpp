// vim: ts=4 sts=4 sw=4 et

#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <site/site.h>
#include <uenv/env.h>
#include <uenv/parse.h>
#include <uenv/repository.h>
#include <util/expected.h>
#include <util/fs.h>

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
    inspect_cli->add_flag("--json", json, "format output as JSON.");
    inspect_cli->add_option("uenv", uenv, "the uenv to inspect.")->required();
    inspect_cli->callback(
        [&settings]() { settings.mode = uenv::cli_mode::image_inspect; });

    inspect_cli->footer(image_inspect_footer);
}

int image_inspect([[maybe_unused]] const image_inspect_args& args,
                  [[maybe_unused]] const global_settings& globals) {
    spdlog::info("image inspect {}", args.uenv);

    // parse input as either a label or a file path
    uenv_description desc;
    if (const auto parse = parse_uenv_description(args.uenv); !parse) {
        term::error("invalid uenv specification: {}", parse.error().message());
        return 1;
    } else {
        desc = parse.value();
    }

    // Resolve the uenv to get full information including metadata
    auto info_result =
        resolve_uenv(desc, globals.config.repo, globals.calling_environment);
    if (!info_result) {
        term::error("unable to resolve uenv: {}", info_result.error());
        return 1;
    }
    const auto& info = *info_result;

    // --json and --format flags are mutually inconsistent
    if (args.json && args.format) {
        term::error(
            "the --json and --format flag can't be set at the same time.");
        return 1;
    }

    // expand all of the information in JSON format to create a single source of
    // truth
    auto value_or_none = [](const auto& x) -> nlohmann::json {
        if (x) {
            return x.value();
        }
        return nullptr;
    };
    nlohmann::json j = {
        // values that are always provided
        {"sqfs", info.sqfs_path.string()},
        {"path", info.sqfs_path.parent_path().string()},
        {"meta", nullptr},
        // values taken from meta data
        {"description", nullptr},
        {"mount", nullptr},
        {"views", nlohmann::json::array()},
        // values taken from repo database
        {"name", nullptr},
        {"version", nullptr},
        {"tag", nullptr},
        {"digest", nullptr},
        {"id", nullptr},
        {"date", nullptr},
        {"system", nullptr},
        {"uarch", nullptr},
    };

    if (info.meta_path && !util::is_temp_dir(info.meta_path.value())) {
        j["meta"] = info.meta_path->string();
    }

    if (info.record) {
        const auto& r = info.record.value();
        j["version"] = r.version;
        j["tag"] = r.tag;
        j["digest"] = fmt::format("{}", r.sha);
        j["id"] = fmt::format("{}", r.id);
        j["date"] = fmt::format("{}", r.date);
        j["system"] = r.system;
        j["uarch"] = r.uarch;
    }

    if (info.meta) {
        j["name"] = info.meta->name;
        j["description"] = value_or_none(info.meta->description);
        j["mount"] = info.meta->mount;
        // TODO: only set meta path if it was not temporarily expanded
        for (const auto& [_, view] : info.meta->views) {
            j["views"].push_back(
                {{"name", view.name}, {"description", view.description}});
        }
    }

    // we use the name from the metadata, not the name from from repo record.
    // create a warning when the name does not match?

    if (info.record && info.meta && info.record->name != info.meta->name) {
        spdlog::warn(
            "the repo and meta data set different names: '{}' and '{}'",
            info.record->name, info.meta->name);
    }

    // mode 1: JSON output
    if (args.json) {
        fmt::print("{}\n", j.dump(2));
    }
    // custom format string
    else if (args.format) {
        // Format views as a comma-separated string for custom format
        std::string views_str = "";
        if (info.meta) {
            std::vector<std::string> view_list;
            for (const auto& [_, view] : info.meta->views) {
                view_list.push_back(
                    fmt::format("{} ({})", view.name, view.description));
            }
            views_str = fmt::format("{}", fmt::join(view_list, ", "));
        }

        try {
            spdlog::debug("inspect format string: '{}'", *args.format);
            auto getvalue = [&j](const auto& idx) -> std::string {
                if (j[idx].is_null()) {
                    return "none";
                }
                return j[idx];
            };

            // clang-format off
            auto msg = fmt::format(
                fmt::runtime(*args.format),
                fmt::arg("name",    getvalue("name")),
                fmt::arg("version", getvalue("version")),
                fmt::arg("tag",     getvalue("tag")),
                fmt::arg("id",      getvalue("id")),
                fmt::arg("digest",  getvalue("digest")),
                fmt::arg("sha256",  getvalue("digest")),
                fmt::arg("date",    getvalue("date")),
                fmt::arg("system",  getvalue("system")),
                fmt::arg("uarch",   getvalue("uarch")),
                fmt::arg("path",    getvalue("path")),
                fmt::arg("sqfs",    getvalue("sqfs")),
                fmt::arg("meta",    getvalue("meta")),
                fmt::arg("mount",   getvalue("mount")),
                fmt::arg("description", getvalue("description")),
                fmt::arg("views",   views_str)
            );
            // clang-format on
            fmt::print("{}\n", msg);
        } catch (fmt::format_error& e) {
            spdlog::error("error formatting output: {}", e.what());
            term::error("unable to format output: {}", e.what());
            return 1;
        }
    }

    // default structured output
    else {
        const std::string label_str =
            info.record ? fmt::format("{}", info.record.value())
                        : info.sqfs_path.string();
        if (info.meta) {
            fmt::print("{} mount at {}\n", label_str, info.meta->mount);
            const auto& views = info.meta->views;
            if (!views.empty()) {
                fmt::print("views:\n");
                for (const auto& [_, view] : views) {
                    fmt::print("  {}: {}\n", view.name, view.description);
                }
            } else {
                fmt::print("views: none\n");
            }
        } else {
            fmt::print(
                "{} can be mounted, but it has no set mount point or views",
                label_str);
        }
    }
    return 0;
}

std::string image_inspect_footer() {
    using enum help::block::admonition;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Display detailed information about a uenv." },
        help::linebreak{},
        help::block{xmpl, "inspect a uenv using a label"},
        help::block{code,   "uenv image inspect prgenv-gnu/24.7:v1"},
        help::block{none, "output:"},
        help::block{none, "  prgenv-gnu/24.7:v1@santis%gh200 mounted at /user-environment"},
        help::block{none, "  views:"},
        help::block{none, "    default: a default environment"},
        help::block{none, "    develop: the development view"},
        help::linebreak{},
        help::block{xmpl, "inspect with JSON output"},
        help::block{code,   "uenv image inspect --json prgenv-gnu/24.7:v1"},
        help::linebreak{},
        help::block{xmpl, "inspect a uenv from a squashfs file path"},
        help::block{code,   "uenv image inspect /path/to/store.squashfs"},
        help::linebreak{},
        help::block{xmpl, "use a custom format string"},
        help::block{code,   "uenv image inspect --format='image {name} at {mount}' prgenv-gnu"},
        help::linebreak{},
        help::block{note, "when using a label, it must uniquely identify the uenv."},
        help::block{none, "If more than one uenv matches the label, an error message is printed."},
        help::linebreak{},
        help::block{info, "Output modes:"},
        help::block{none, "  default:  structured output showing label, mount point, and views"},
        help::block{none, "  --json:   JSON format with views as array of {name, description}"},
        help::block{none, "  --format: custom format string using variables below"},
        help::linebreak{},
        help::block{info, "format string variables (for use with --format):"},
        help::block{none, "    name:        name from metadata (\"none\" if no metadata)"},
        help::block{none, "    description: description from metadata"},
        help::block{none, "    mount:       mount point from metadata"},
        help::block{none, "    views:       comma-separated list of views with descriptions"},
        help::block{none, "    version:     version (only available for labels)"},
        help::block{none, "    tag:         tag (only available for labels)"},
        help::block{none, "    id:          the 16 digit image id (only available for labels)"},
        help::block{none, "    sha256:      the unique sha256 hash"},
        help::block{none, "    date:        creation date (only for labels)"},
        help::block{none, "    system:      the system the uenv was built for (only for labels)"},
        help::block{none, "    uarch:       the micro-architecture (only for labels)"},
        help::block{none, "    path:        absolute path where the uenv is stored"},
        help::block{none, "    sqfs:        absolute path of the squashfs file"},
        help::block{none, "    meta:        absolute path of the metadata directory"},
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace uenv
