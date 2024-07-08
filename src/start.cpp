// vim: ts=4 sts=4 sw=4 et

#include <fmt/core.h>

#include "start.h"
#include "uenv.h"

namespace uenv {

void start_help() {
    fmt::println("start help");
}

void start_options::add_cli(CLI::App &cli, global_settings &settings) {
    auto *start_cli = cli.add_subcommand("start", "start a uenv session");
    start_cli->add_option("-v,--view", view_str, "comma separated list of views to load");
    start_cli->add_option("uenv", image_str, "comma separated list of uenv to mount")->required();
    start_cli->callback([&settings]() { settings.mode = uenv::mode_start; });
}

void start(const start_options &options, const global_settings &settings) {
    fmt::println("running start with options {}", options);
}

} // namespace uenv
