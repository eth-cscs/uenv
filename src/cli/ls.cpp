// vim: ts=4 sts=4 sw=4 et

// #include <string>

#include <fmt/core.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/cscs.h>
#include <uenv/parse.h>
#include <uenv/repository.h>
#include <util/expected.h>

#include "ls.h"

namespace uenv {

void image_ls_help() {
    fmt::println("image ls help");
}

void image_ls_args::add_cli(CLI::App& cli,
                            [[maybe_unused]] global_settings& settings) {
    auto* ls_cli = cli.add_subcommand("ls", "manage and query uenv images");
    ls_cli->add_option("uenv", uenv_description,
                       "comma separated list of uenv to mount.");
    ls_cli->add_flag("--no-header", no_header,
                     "print only the matching records, with no header.");
    ls_cli->callback([&settings]() { settings.mode = uenv::mode_image_ls; });
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
        fmt::println("{:<{}}{:<{}}{:<{}}{:<18}", "uenv", w_name, "arch", w_arch,
                     "system", w_sys, "id");
    }
    for (auto& r : *result) {
        auto name = fmt::format("{}/{}:{}", r.name, r.version, r.tag);
        fmt::println("{:<{}}{:<{}}{:<{}}{:<18}", name, w_name, r.uarch, w_arch,
                     r.system, w_sys, r.id.string());
    }

    return 0;
}

} // namespace uenv
