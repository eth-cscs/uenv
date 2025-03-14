#include <filesystem>
#include <optional>

#include <spdlog/spdlog.h>

#include <uenv/parse.h>
#include <uenv/repository.h>
#include <uenv/settings.h>
#include <util/color.h>
#include <util/environment.h>
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
    return {
        .repo = lhs.repo   ? lhs.repo
                : rhs.repo ? rhs.repo
                           : std::nullopt,
        .color = lhs.color   ? lhs.color
                 : rhs.color ? rhs.color
                             : std::nullopt,
    };
}

config_base default_config(const environment::variables& env) {
    return {
        .repo = default_repo_path(env),
        .color = color::default_color(env),
    };
}

util::expected<configuration, std::string>
generate_configuration(const config_base& base) {
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

    return config;
}

// forward declare implementation
namespace impl {
util::expected<config_base, std::string>
read_config_file(const std::filesystem::path& path,
                 const environment::variables& calling_env);
}

util::expected<config_base, std::string>
load_user_config(const environment::variables& calling_env) {
    namespace fs = std::filesystem;

    auto home_env = calling_env.get("HOME");
    auto xdg_env = calling_env.get("XDG_CONFIG_HOME");
    // return an empty config if no configuration path can be determined
    if (!home_env && !xdg_env) {
        spdlog::warn("unable to find default configuration location, neither "
                     "HOME nor XDG_CONFIG_HOME are defined.");
        return config_base{};
    }
    const auto config_path = xdg_env ? (fs::path(xdg_env.value()) / "uenv")
                                     : (fs::path(home_env.value()) / "uenv");
    const auto config_file = config_path / "config";

    auto create_config_file = [](const auto& path) {
        auto fid = std::ofstream(path);
        fid << config_file_default << std::endl;
    };
    if (!fs::exists(config_path)) {
        spdlog::info("creating configuration path {}", config_path);
        std::error_code ec;
        fs::create_directories(config_path, ec);
        if (ec) {
            spdlog::error("unable to create config path: {}", ec.message());
            return config_base{};
        }
        spdlog::info("creating configuration file {}", config_file);
        create_config_file(config_file);
        return config_base{};
    } else if (!fs::exists(config_file)) {
        spdlog::info("creating configuration file {}", config_file);
        create_config_file(config_file);
        return config_base{};
    }

    auto result = impl::read_config_file(config_file, calling_env);

    if (!result) {
        return util::unexpected{fmt::format(
            "error opening '{}': {}", config_file.string(), result.error())};
    }

    return *result;
}

namespace impl {
util::expected<config_base, std::string>
read_config_file(const std::filesystem::path& path,
                 const environment::variables& calling_env) {
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
                calling_env.expand(value, environment::expand_delim::curly);
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
        } else {
            return util::unexpected(
                fmt::format("invalid configuration parameter '{}'", key));
        }
    }

    return config;
}
} // namespace impl

} // namespace uenv
