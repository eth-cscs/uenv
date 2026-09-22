// vim: ts=4 sts=4 sw=4 et

#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <oci/auth.h>
#include <oci/client.h>
#include <oci/reference.h>
#include <oci/tag.h>
#include <oci/types.h>
#include <uenv/parse.h>
#include <uenv/print.h>
#include <uenv/repository.h>
#include <util/expected.h>
#include <util/fs.h>
#include <util/signal.h>

#include "delete.h"
#include "help.h"
#include "terminal.h"
#include "util.h"

namespace uenv {

namespace {

struct image_delete_args {
    std::string uenv_description;
    std::optional<std::string> token;
    std::optional<std::string> username;
};

std::string image_delete_footer();

int image_delete(const image_delete_args& args,
                 const global_settings& settings);

} // namespace

argparse::command image_delete_command(const global_settings& settings) {
    using argparse::completion;
    argparse::command_builder<image_delete_args> delete_cli(
        "delete", "delete a uenv from a remote registry");
    delete_cli
        .add_positional("uenv", &image_delete_args::uenv_description,
                        "either name/version:tag, sha256 or id")
        .required()
        .complete(completion::custom("registry_nslabel"));
    delete_cli
        .add_option(
            "token", &image_delete_args::token,
            "a path that contains a TOKEN file for accessing the registry")
        .complete(completion::path());
    delete_cli
        .add_option("username", &image_delete_args::username,
                    "user name for the registry (by default $USER is used).")
        .complete(completion::none());
    delete_cli.action([&settings](const image_delete_args& args) {
        return image_delete(args, settings);
    });

    delete_cli.footer(image_delete_footer);

    return std::move(delete_cli).build();
}

namespace {

int image_delete([[maybe_unused]] const image_delete_args& args,
                 [[maybe_unused]] const global_settings& settings) {
    if (!settings.config.registry) {
        term::error("registry is not configured: add a [registry] section to "
                    "your uenv configuration file");
        return 1;
    }
    const auto& registry_cfg = *settings.config.registry;

    std::optional<oci::credentials> credentials;
    if (auto c = resolve_registry_credentials(settings.calling_environment,
                                              registry_cfg.url, args.username,
                                              args.token)) {
        credentials = *c;
    } else {
        term::error("{}", c.error());
        return 1;
    }

    uenv_label label{};
    std::string nspace{};
    if (const auto parse = parse_uenv_nslabel(args.uenv_description)) {
        label = parse->label;
        if (!label.name || !parse->nspace) {
            term::error("the uenv {} must provide at least a namespace and "
                        "name, e.g. 'build::f7076704830c8de7'",
                        args.uenv_description);
            return 1;
        }
        nspace = parse->nspace.value();
    } else {
        term::error("invalid uenv: {}", parse.error().message());
        return 1;
    }
    spdlog::debug("requested to delete {}::{}", nspace, label);

    auto registry = fetch_registry_listing(settings, nspace);
    if (!registry) {
        term::error("unable to get a listing of the uenv: {}",
                    registry.error());
        return 1;
    }

    // search db for matching records
    const auto matches = registry->query(label);
    if (!matches) {
        term::error("invalid search term: {}", registry.error());
        return 1;
    }
    // check that there is one record with a unique sha
    if (matches->empty()) {
        using enum help::block::admonition;
        term::error("no uenv found that matches '{}'\n\n{}",
                    args.uenv_description,
                    help::block(info, "try searching for the uenv to copy "
                                      "first using 'uenv image find'"));
        return 1;
    } else if (!matches->unique_sha()) {
        std::string errmsg =
            fmt::format("more than one sha found that matches '{}':\n",
                        args.uenv_description);
        errmsg += format_record_set_table(*matches);
        term::error("{}", errmsg);
        return 1;
    }

    const auto loc = oci::split_registry(registry_cfg.url);

    for (auto& record : *matches) {
        const auto repository =
            oci::repository_path(loc.prefix, nspace, record.system,
                                 record.uarch, record.name, record.version);

        auto client = oci::client::create(loc.base, repository, credentials);
        if (!client) {
            term::error("unable to connect to the registry: {}",
                        client.error());
            return 1;
        }

        // the record's tag comes from the listing service, so it is not
        // guaranteed to be a well-formed OCI tag until it is parsed.
        auto tag = oci::tag::parse(record.tag);
        if (!tag) {
            term::error("invalid tag '{}': {}", record.tag,
                        tag.error().message());
            return 1;
        }

        // delete the *tag*, never the digest: other tags may point at the same
        // manifest, and deleting by digest would take them with it.
        if (auto result = client->delete_manifest(oci::reference::tag(*tag));
            !result) {
            const auto& err = result.error();
            if (err.http_status == 404) {
                term::error("{}::{} is not in the registry: the listing may be "
                            "out of date",
                            nspace, record);
            } else if (err.http_status == 405 || err.http_status == 501) {
                term::error("this registry does not support deleting tags "
                            "through the OCI API ({})",
                            err.message);
            } else {
                term::error("unable to delete {}::{}: {}", nspace, record,
                            err.message);
            }
            return 1;
        }

        term::msg("deleted {}::{}", nspace, record);
    }

    return 0;
}

std::string image_delete_footer() {
    using enum help::block::admonition;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Delete a uenv from a remote registry." },
        help::linebreak{},
        help::linebreak{},
        help::block{xmpl, "delete a uenv from a namespace"},
        help::block{code,   "uenv image delete build::prgenv-gnu/24.11:1551223269@todi%gh200"},
        help::block{code,   "uenv image delete build::7890d67458ce7deb"},
        help::block{code,   "uenv image delete deploy::prgenv-gnu/24.11:rc1@todi"},
        help::linebreak{},
        help::block{note, "the requested uenv must resolve to a unique sha."},
        help::block{none, "only the tag is deleted: other tags that refer to the" },
        help::block{none, "same uenv are not affected." },
        help::linebreak{},
        help::block{xmpl, "use a token for the registry"},
        help::block{code,   "uenv image delete --token=/opt/cscs/uenv/tokens/vasp6 \\"},
        help::block{code,   "                  build::vasp/6.4.2:1551223269@todi%gh200"},
        help::block{note, "credentials are resolved in order: --token, then the" },
        help::block{none, "uenv token store $XDG_CONFIG_HOME/uenv/tokens/<registry>," },
        help::block{none, "then ~/.docker/config.json." },
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

} // namespace

} // namespace uenv
