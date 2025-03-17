// vim: ts=4 sts=4 sw=4 et
#include <string>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <util/subprocess.h>

#include "build.h"
#include "cli/terminal.h"
#include "fmt/base.h"
#include "help.h"
#include "site/site.h"
#include "uenv/parse.h"
#include "util/curl.h"
#include "util/fs.h"

namespace uenv {

std::string build_footer();

std::string format_reply(const std::string&);

void build_args::add_cli(CLI::App& cli,
                         [[maybe_unused]] global_settings& settings) {
    auto* build_cli =
        cli.add_subcommand("build", "build a uenv from a local recipe");
    build_cli->add_flag("-d,--develop", spack_develop, "Assume spack@develop");
    build_cli->add_option("recipe", uenv_recipe_path, "Path to uenv recipe")
        ->required(true);
    build_cli
        ->add_option("label", uenv_label,
                     "UENV description: <name>/<version>@<system>%<uarch>")
        ->required(true);
    build_cli->footer(build_footer);
    build_cli->callback([&settings] { settings.mode = settings.build; });
}

std::string build_footer() {
    using enum help::block::admonition;
    using help::lst;
    std::vector<help::item> items{
        // clang-format off
        help::block{none, "Build uenv images." },
        help::linebreak{},
        help::block{none, fmt::format("For more information on how to use individual commands use the {} flag.", lst("--help")) },
        help::linebreak{},
        help::block{xmpl, "Build QuantumESPRESSO UENV" },
        help::block{code, "uenv build /path/to/QE/recipe quantumespresso/v7.3.1@daint%gh200"},
        help::linebreak{},
        // clang-format on
    };

    return fmt::format("{}", fmt::join(items, "\n"));
}

int build(const build_args& args,
          [[maybe_unused]] const global_settings& settings) {

    auto label = parse_uenv_label(args.uenv_label);

    if (!label) {
        term::error("Couldn't parse label: {}", label.error().message());
        return 1;
    }

    if (!(label->name && label->version && label->system && label->uarch &&
          !label->tag)) {
        term::error(
            "description must be given in the form name/version@system%uarch");
        return 1;
    }

    std::filesystem::path recipe_path{args.uenv_recipe_path};

    if (!std::filesystem::is_directory(recipe_path)) {
        term::error("{} not a directory", recipe_path);
        return 1;
    }

    // check the directory contains the expected files
    for (std::string f :
         {"compilers.yaml", "config.yaml", "environments.yaml"}) {
        if (!std::filesystem::is_regular_file(recipe_path / f)) {
            term::error("{} doesn't contain {}", recipe_path, f);
            return 1;
        }
    }

    // get system from args or autodetect
    auto system =
        site::get_system_name(label->system, settings.calling_environment);
    if (!system) {
        term::error("Couldn't auto-detect system name. Please specify it "
                    "explicitly using desc@<system>");
        return 1;
    }

    auto recipe_tar_path = util::make_temp_dir() / "recipe.tar.gz";
    auto proc = util::run({"env", "--chdir", recipe_path.string(), "tar",
                           "--dereference", "-czf", recipe_tar_path, "."});
    if (proc->rvalue() > 0) {
        term::error("{}", proc->err.string());
        return 1;
    }

    std::vector<std::string> vars{fmt::format("system={}", *system),
                                  fmt::format("uarch={}", *label->uarch),
                                  fmt::format("name={}", *label->name),
                                  fmt::format("version={}", *label->version)};

    if (args.spack_develop) {
        vars.push_back("SPACK_DEVELOP=-d");
    }

    auto cicd_endpoint = fmt::format("https://cicd-ext-mw.cscs.ch/ci/uenv/build"
                                     "?{}",
                                     fmt::join(vars, "&"));
    auto res = util::curl::upload(cicd_endpoint, recipe_tar_path);

    if (!res) {
        term::error("uenv build submission failed: {}", res.error().message);
        return 1;
    }

    // print summary returned by CICD-ext
    try {
        term::msg("{}", format_reply(res.value()));
    } catch (nlohmann::json::basic_json::out_of_range& e) {
        term::error("unable to parse build service reply - please forward this "
                    "error message along with the arguments to the CSCS "
                    "Service Desk.\n{}\n{}",
                    e.what(), res.value());
        return 1;
    }
    return 0;
}

std::string format_reply(const std::string& reply) {
    auto data = nlohmann::json::parse(reply);
    return fmt::format(
        R"(
Log         : {log}
Status      : {status}

Destination
Registry    : {registry}
Namespace   : {namespace}
Label       : {label}
)",
        fmt::arg("log", data.at("build_log_url").get<std::string>()),
        fmt::arg("status", data.at("status").get<std::string>()),
        fmt::arg("registry",
                 data["destination"].at("registry").get<std::string>()),
        fmt::arg("namespace",
                 data["destination"].at("namespace").get<std::string>()),
        fmt::arg("label", data["destination"].at("label").get<std::string>()));
}

} // namespace uenv
