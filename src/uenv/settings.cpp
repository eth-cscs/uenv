#include "util/expected.h"
#include <filesystem>
#include <fstream>
#include <optional>

#include <fmt/format.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include <uenv/parse.h>
#include <uenv/repository.h>
#include <uenv/settings.h>
#include <util/color.h>
#include <util/envvars.h>
#include <util/strings.h>

namespace uenv {

const std::string config_file_default =
    R"(
# uenv configuration file
# lines starting with '#' are comments

# set the path to the local uenv repository
#repo = /users/bobsmith/uenv

# by default uenv will choose whether to use color based on your environment.
#color=true
#color=false
)";

// merge two config_base items
// if both have the same field set, choose the lhs value
config_base merge(const config_base& lhs, const config_base& rhs) {
    return {.repo = lhs.repo   ? lhs.repo
                    : rhs.repo ? rhs.repo
                               : std::nullopt,
            .color = lhs.color   ? lhs.color
                     : rhs.color ? rhs.color
                                 : std::nullopt,
            .elastic_config = lhs.elastic_config   ? lhs.elastic_config
                              : rhs.elastic_config ? rhs.elastic_config
                                                   : std::nullopt};
}

config_base default_config(const envvars::state& env) {
    // find whether a repo exists in the list of possible default repo loations
    auto rexist = default_repo_path(env, true);
    // find the recommended repo location (if one is available)
    auto ravail = default_repo_path(env, false);

    // if the default repository is not at the recommended location, print a
    // warning and suggestion that the user upgrade to a new uenv
    // NOTE: the backend library code is not supposed to print to the terminal,
    // but we make an exception in this case to get this feature in place.
    if (rexist && (rexist != ravail)) {
        fmt::print(stderr,
                   "\n{}: the default uenv repo location is being used. Please "
                   "migrate your uenv repo with:\n  uenv repo migrate {}\n\n",
                   color::yellow("warning"), ravail.value());
    }
    return {
        .repo = rexist ? rexist : ravail,
        .color = color::default_color(env),
    };
}

configuration generate_configuration(const config_base& base) {
    configuration config;

    // set the repo path
    // initialise to unset
    config.repo = {};
    if (base.repo) {
        if (auto path = parse_path(base.repo.value())) {
            if (auto rpath =
                    uenv::validate_repo_path(path.value(), false, false)) {
                config.repo = path.value();
            } else {
                spdlog::warn("invalid repo path {}", rpath.error());
            }
        } else {
            spdlog::warn("invalid repo path {}", path.error().message());
        }
    }

    // toggle color output
    config.color = base.color.value_or(false);

    config.elastic_config = base.elastic_config;

    return config;
}

// forward declare implementation
namespace impl {
util::expected<config_base, std::string>
read_config_file(const std::filesystem::path& path,
                 const envvars::state& calling_env);
}

// read configuration from the user configuration file
// the location of the config file is determined using XDG_CONFIG_HOME or HOME
util::expected<config_base, std::string>
load_user_config(const envvars::state& calling_env) {
    namespace fs = std::filesystem;

    auto home_env = calling_env.get("HOME");
    auto xdg_env = calling_env.get("XDG_CONFIG_HOME");
    // return an empty config if no configuration path can be determined
    if (!home_env && !xdg_env) {
        spdlog::warn("unable to find default configuration location, neither "
                     "HOME nor XDG_CONFIG_HOME are defined.");
        return config_base{};
    }
    const auto config_path =
        xdg_env ? (fs::path(xdg_env.value()) / "uenv")
                : (fs::path(home_env.value()) / ".config/uenv");
    const auto config_file = config_path / "config";

    auto create_config_file = [](const auto& path) {
        auto fid = std::ofstream(path);
        fid << config_file_default << std::endl;
    };
    if (!fs::exists(config_path)) {
        spdlog::debug("load_user_config:: creating configuration path {}",
                      config_path);
        std::error_code ec;
        fs::create_directories(config_path, ec);
        if (ec) {
            spdlog::error("load_user_config::unable to create config path: {}",
                          ec.message());
            return config_base{};
        }
        spdlog::debug("load_user_config::creating configuration file {}",
                      config_file);
        create_config_file(config_file);
        return config_base{};
    } else if (!fs::exists(config_file)) {
        spdlog::debug("load_user_config::creating configuration file {}",
                      config_file);
        create_config_file(config_file);
        return config_base{};
    }

    spdlog::debug("load_user_config:: opening {}", config_file);
    auto result = impl::read_config_file(config_file, calling_env);

    if (!result) {
        return util::unexpected{fmt::format(
            "error opening '{}': {}", config_file.string(), result.error())};
    }

    spdlog::info("load_user_config:: loaded {}", config_path);

    return *result;
}

// read configuration from /etc
util::expected<config_base, std::string>
load_system_config(const envvars::state& calling_env) {
    namespace fs = std::filesystem;

    const auto config_path = fs::path(
        calling_env.get("UENV_SYSTEM_CONFIG").value_or("/etc/uenv/config"));
    spdlog::trace("load_system_config::using {}", config_path.string());

    if (!fs::exists(config_path)) {
        return util::unexpected(fmt::format(
            "load_system_config::path {} does not exist", config_path));
    }

    auto result = impl::read_config_file(config_path, calling_env);
    if (!result) {
        return util::unexpected{fmt::format(
            "load_system_config::error reading config {}", result.error())};
    }

    spdlog::info("load_system_config:: loaded {}", config_path);
    return result;
}

config_base load_config(const uenv::config_base& cli_config,
                        const envvars::state& calling_env) {
    auto config = uenv::default_config(calling_env);
    if (auto sys = uenv::load_system_config(calling_env)) {
        config = merge(*sys, config);
    } else {
        spdlog::info("load_config::did not load system config file: {}",
                     sys.error());
    }
    if (auto usr = uenv::load_user_config(calling_env)) {
        config = merge(*usr, config);
    } else {
        spdlog::info("load_config::did not load user config: {}", usr.error());
    }
    return merge(cli_config, config);
}

namespace impl {
util::expected<config_base, std::string>
read_config_file(const std::filesystem::path& path,
                 const envvars::state& calling_env) {
    namespace fs = std::filesystem;

    if (!fs::exists(path) || !std::filesystem::is_regular_file(path)) {
        return util::unexpected{"file does not exist or is not a regular file"};
    }

    // open the configuration file
    std::ifstream fid(path);
    if (!fid.is_open()) {
        return util::unexpected{"unable to open file"};
    }

    // parse the file line by line
    // generate a hash table of (key, value) where all keys and values are
    // strings
    std::string line;
    std::unordered_map<std::string, std::string> settings;
    unsigned lineno = 0;
    while (std::getline(fid, line)) {
        if (const auto result = parse_config_line(line)) {
            if (*result) {
                if (settings.contains(result->key)) {
                    spdlog::warn(
                        "the configuration parameter {} is defined more than "
                        "once (line {})",
                        result->key, lineno);
                }
                settings[result->key] = result->value;
            }
        } else {
            return util::unexpected{fmt::format("{}:{}\n  {}", lineno, line,
                                                result.error().message())};
        }
        ++lineno;
    }

    fid.close();

    // build a config from the key value
    config_base config;
    for (auto [key, value] : settings) {
        if (key == "repo") {
            config.repo =
                calling_env.expand(value, envvars::expand_delim::curly);
        } else if (key == "color") {
            if (value == "true") {
                config.color = true;
            } else if (value == "false") {
                config.color = false;
            } else {
                return util::unexpected(
                    fmt::format("invalid configuration value '{}={}': color "
                                "muste be true or false",
                                key, value));
            }
        } else if (key == "elasticsearch") {
            config.elastic_config = value;
        } else {
            return util::unexpected(
                fmt::format("invalid configuration parameter '{}'", key));
        }
    }

    return config;
}
} // namespace impl

} // namespace uenv
