// vim: ts=4 sts=4 sw=4 et

#include <optional>
#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/parse.h>
#include <uenv/pull.h>

#include "help.h"
#include "pull.h"
#include "terminal.h"
#include "util.h"

namespace uenv {

namespace {

struct image_pull_args {
    std::string uenv_description;
    std::optional<std::string> token;
    std::optional<std::string> username;
    bool only_meta = false;
    bool force = false;
};

std::string image_pull_footer();

int image_pull(const image_pull_args& args, const global_settings& settings);

} // namespace

argparse::command image_pull_command(const global_settings& settings) {
    using argparse::completion;
    argparse::command_builder<image_pull_args> pull_cli(
        "pull", "download a uenv from a registry");
    pull_cli
        .add_positional(
            "uenv", &image_pull_args::uenv_description,
            "the uenv to pull, either name/version:tag, sha256 or id")
        .required()
        .complete(completion::custom("registry_label"));
    pull_cli
        .add_option(
            "token", &image_pull_args::token,
            "a path that contains a TOKEN file for accessing restricted uenv")
        .complete(completion::path());
    pull_cli
        .add_option("username", &image_pull_args::username,
                    "user name for accessing restricted uenv.")
        .complete(completion::none());
    pull_cli.add_flag("only-meta", &image_pull_args::only_meta,
                      "only download meta data");
    pull_cli.add_flag("force", &image_pull_args::force,
                      "download and overwrite existing images");
    pull_cli.action([&settings](const image_pull_args& args) {
        return image_pull(args, settings);
    });

    pull_cli.footer(image_pull_footer);

    return std::move(pull_cli).build();
}

namespace {

int image_pull(const image_pull_args& args, const global_settings& settings) {
    if (!settings.config.registry) {
        term::error("registry is not configured: add a [registry] section to "
                    "your uenv configuration file");
        return 1;
    }
    const auto& registry_cfg = *settings.config.registry;

    // Resolve credentials with --token/--username support.
    std::optional<oci::credentials> credentials;
    if (auto c = resolve_registry_credentials(settings.calling_environment,
                                              registry_cfg.url, args.username,
                                              args.token)) {
        credentials = *c;
    } else {
        term::error("{}", c.error());
        return 1;
    }

    // Parse the search term with namespace support.
    uenv_label label{};
    std::string nspace{registry_cfg.default_namespace};
    if (const auto parse = parse_uenv_nslabel(args.uenv_description)) {
        label = parse->label;
        if (parse->nspace) {
            nspace = *parse->nspace;
        }
    } else {
        term::error("invalid search term: {}", parse.error().message());
        return 1;
    }

    label = apply_system(label, settings.config.system_name);
    if (!label.name) {
        term::error(
            "the uenv description '{}' must specify the name of the uenv",
            args.uenv_description);
        return 1;
    }

    // Progress bar callbacks: the bar is created in on_pull_start (which
    // receives the record with size_byte) and torn down in on_pull_end.
    std::unique_ptr<transfer_bar> bar;
    auto cbs = uenv::pull_callbacks{
        .on_pull_start = [&bar](const uenv::uenv_record& r) {
            bar = make_transfer_bar(r.size_byte,
                                    fmt::format("pulling {}", r.id.string()));
        },
        .progress = [&bar](std::uint64_t now, std::uint64_t) {
            if (bar) {
                bar->update(now);
            }
        },
        .on_pull_end = [&bar](bool success) {
            if (bar) {
                success ? bar->finish() : bar->stop();
                bar.reset();
            }
        },
    };

    auto result = uenv::pull_uenv(
        label, nspace, settings.config, settings.calling_environment,
        {.force = args.force, .only_meta = args.only_meta,
         .creds = credentials, .callbacks = cbs});
    if (!result) {
        term::error("{}", result.error());
        return 1;
    }

    if (!result->downloaded) {
        term::msg("id={} already exists in the repository, skipping pull.",
                  result->record.id.string());
    }

    for (auto& r : result->added) {
        term::msg("updating {}", r);
    }

    return 0;
}

std::string image_pull_footer() {
    using enum help::block::admonition;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Download a uenv from a registry." },
        help::linebreak{},
        help::linebreak{},
        help::block{xmpl, "pull a uenv"},
        help::block{code,   "uenv image pull prgenv-gnu"},
        help::block{code,   "uenv image pull prgenv-gnu/24.11:v1@todi"},
        help::linebreak{},
        help::block{xmpl, "use a token for the registry"},
        help::block{code,   "uenv image pull --token=/opt/cscs/uenv/tokens/vasp6 vasp/6.4.2:v1"},
        help::block{note, "this is only required when accessing uenv that require special" },
        help::block{none, "permission or a license to access." },
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace

} // namespace uenv
