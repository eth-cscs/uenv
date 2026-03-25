#include <filesystem>
#include <fstream>
#include <optional>
#include <set>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>
#include <toml++/toml.hpp>

#include <uenv/parse.h>
#include <uenv/repository.h>
#include <uenv/settings.h>
#include <util/color.h>
#include <util/envvars.h>
#include <util/expected.h>
#include <util/strings.h>

namespace uenv {

const std::string config_file_default =
    R"(# uenv configuration file
# in TOML v1.0 format, see https://toml.io

# by default uenv will choose whether to use color based on your environment.
#color = true
#color = false

# set the path to the local uenv repository
#[[repositories]]
#name = 'team'
#path = '/store/g123/uenv/repo'
)";

util::unexpected<config_error> make_config_error(std::string message,
                                                 std::uint32_t line) {
    return util::unexpected{
        config_error{.message = std::move(message), .line = line}};
}

std::optional<uenv::repo_description> configuration::repo() const {
    if (repos.empty()) {
        return std::nullopt;
    }
    return repos[0];
}

// merge two config_base items
// if both have the same field set, choose the lhs value
config_base merge(const config_base& lhs, const config_base& rhs) {
    auto repos = lhs.repos;
    repos.insert(repos.end(), rhs.repos.begin(), rhs.repos.end());
    return {.repos = repos,
            .color = lhs.color   ? lhs.color
                     : rhs.color ? rhs.color
                                 : std::nullopt,
            .elastic_config = lhs.elastic_config   ? lhs.elastic_config
                              : rhs.elastic_config ? rhs.elastic_config
                                                   : std::nullopt};
}

config_base default_config(const envvars::state& env) {
    // find whether a repo exists in the list of possible default repo
    // loations
    auto rexist = default_repo_path(env, true);
    // find the recommended repo location (if one is available)
    auto ravail = default_repo_path(env, false);

    // if the default repository is not at the recommended location, print a
    // warning and suggestion that the user upgrade to a new uenv
    // NOTE: the backend library code is not supposed to print to the
    // terminal, but we make an exception in this case to get this feature
    // in place.
    if (rexist && (rexist != ravail) && !env.get("UENV_WARN_MIGRATE")) {
        // clang-format off
        fmt::print(
            stderr,
            "--------------------------------------------------------------------------------\n"
            "{}: the default uenv repo on this system has moved to a new location:\n"
            "  {}\n"
            "Migrate your repo, while the old location is still available, with this command:\n"
            "  {}\n"
            "  {}\n"
            "Migration can take over 30 minutes, and must be completed fully after it has\n"
            "been started for all of the original images to be available. If interrupted,\n"
            "migration can be resumed using the same command.\n"
            "{}: uenv will continue using the old location and printing this warning until\n"
            "the migration is performed.\n"
            "Set the environment variable UENV_WARN_MIGRATE to silence this warning.\n"
            "--------------------------------------------------------------------------------\n",
            color::yellow("warning"),
            color::cyan(ravail->string()),
            color::cyan(fmt::format("uenv repo migrate {} \\", *rexist)),
            color::cyan(fmt::format("                  {}",  *ravail)),
            color::yellow("note"));
        // clang-format on
    }

    if (rexist || ravail) {
        return {
            .repos = {{.name = "default",
                       .path = (rexist ? *rexist : *ravail)}},
            .color = color::default_color(env),
        };
    }

    return {
        .color = color::default_color(env),
    };
}

configuration generate_configuration(const config_base& base) {
    configuration config;

    for (auto& repo : base.repos) {
        if (auto path = parse_path(repo.path)) {
            if (auto rpath =
                    uenv::validate_repo_path(path.value(), false, false)) {
                config.repos.push_back(repo);
            } else {
                spdlog::warn("invalid repo path {}", rpath.error());
            }
        } else {
            spdlog::warn("invalid repo path {}", path.error().message());
        }
    }
    // TODO: sort will not be needed if we manage repos in their own type?
    std::stable_sort(config.repos.begin(), config.repos.end());

    // disable color output if it has not be enabled/disabled
    config.color = base.color.value_or(false);

    config.elastic_config = base.elastic_config;

    return config;
}

// forward declare implementation
namespace impl::v1 {
util::expected<config_base, std::string>
read_config_file(const std::filesystem::path& path,
                 const envvars::state& calling_env);
}

namespace impl::v2 {
util::expected<config_base, std::string>
read_config_file(const std::filesystem::path& path,
                 const envvars::state& calling_env);
}

// read configuration from the user configuration file
// the location of the config file is determined using XDG_CONFIG_HOME or
// HOME
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
    const auto config_file = config_path / "config.toml";

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
    auto result = impl::v2::read_config_file(config_file, calling_env);

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

    spdlog::debug("load_system_config");

    const auto config_root = fs::path{"/etc/uenv"};
    if (const auto config_path = config_root / "config.toml";
        fs::exists(config_path)) {
        auto result = impl::v2::read_config_file(config_path, calling_env);
        if (!result) {
            return util::unexpected{fmt::format(
                "load_system_config::error reading config {}", result.error())};
        }
        spdlog::info("load_system_config:: loaded {}", config_path);
        return result;
    }
    // TODO: remove this fall back to the old parser when /etc/uenv/config is no
    // longer used
    else if (const auto config_path = config_root / "config";
             fs::exists(config_path)) {
        spdlog::warn(
            "load_system_config:: loading v1 configuration in {} instead of "
            "new toml format",
            config_path);
        auto result = impl::v1::read_config_file(config_path, calling_env);
        if (!result) {
            return util::unexpected{fmt::format(
                "load_system_config::error reading config {}", result.error())};
        }
        spdlog::info("load_system_config:: loaded {}", config_path);
        return result;
    } else {
        spdlog::info("load_system_config:: no configuration file found",
                     config_path);
        return util::unexpected(
            fmt::format("load_system_config::path {} does not exist",
                        config_root / "config.toml"));
    }
}

util::expected<config_base, std::string>
load_config(const uenv::config_base& cli_config,
            const std::optional<std::vector<repo_label>>& repos,
            const envvars::state& calling_env) {
    auto config = uenv::default_config(calling_env);

    if (auto sys = uenv::load_system_config(calling_env)) {
        config = merge(*sys, config);
    } else {
        spdlog::warn("load_config::did not load system config file: {}",
                     sys.error());
    }

    if (auto usr = uenv::load_user_config(calling_env)) {
        config = merge(*usr, config);
    } else {
        spdlog::warn("load_config::did not load user config: {}", usr.error());
    }

    if (repos) {
        auto descriptions = filter_repo_list(*repos, config.repos);

        if (!descriptions) {
            return util::unexpected{fmt::format("{}", descriptions.error())};
        }

        // delete all repos in the accumulate config
        config.repos = {};

        // make a copy of cli_config and replace its repos with the
        // cli-provided list
        auto modified_cli_config = cli_config;
        modified_cli_config.repos = descriptions.value();

        return merge(modified_cli_config, config);
    }

    return merge(cli_config, config);
}

namespace impl::v1 {

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
                    spdlog::warn("the configuration parameter {} is "
                                 "defined more than "
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
            config.repos = {{.name = "default",
                             .path = calling_env.expand(
                                 value, envvars::expand_delim::curly)}};
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

} // namespace impl::v1

namespace impl::v2 {

util::expected<std::vector<repo_description>, config_error>
parse_repository_array(const toml::node& input,
                       const envvars::state& calling_env) {
    const auto arr = input.as_array();
    if (!arr || arr->size() != 1u) {
        return make_config_error("repositories is not an array with 1 entry",
                                 input.source().begin.line);
    }

    std::vector<repo_description> result;
    result.reserve(arr->size());
    for (const auto& element : *arr) {
        std::optional<std::string> path{};
        std::optional<std::string> name{};
        auto priority = repo_description::default_priority;
        if (const auto tbl = element.as_table()) {
            for (const auto& entry : *tbl) {
                std::string_view key = entry.first.str();
                const auto& value = entry.second;
                if (key == "path") {
                    if (auto v = value.value<std::string>()) {
                        std::string expanded = calling_env.expand(
                            v.value(), envvars::expand_delim::curly);
                        if (parse_path(expanded)) {
                            path = std::move(expanded);
                        } else {
                            return make_config_error(
                                "repository.path must be a string "
                                "describing a "
                                "valid path",
                                value.source().begin.line);
                        }
                    } else {
                        return make_config_error(
                            "repository.path must be a string describing a "
                            "valid path",
                            value.source().begin.line);
                    }
                } else if (key == "name") {
                    if (auto v = value.value<std::string>();
                        v && parse_repo_name(v.value())) {
                        name = v.value();
                    } else {
                        return make_config_error("repository.name is not valid",
                                                 value.source().begin.line);
                    }
                } else {
                    return make_config_error(
                        fmt::format("unexpected key '{}'", key),
                        value.source().begin.line);
                }
            }
            if (!name) {
                return make_config_error("repository.name is not defined",
                                         element.source().begin.line);
            }
            if (!path) {
                return make_config_error("repository.path is not defined",
                                         element.source().begin.line);
            }
            // TODO: validate the inputs here
            // this should only generate warnings if an invalid repo is
            // specified, because an invalid description read from a system
            // configuration can't be modified by the user.
            result.push_back({.name = std::move(name.value()),
                              .path = std::move(path.value()),
                              .priority = priority});
        } else {
            return make_config_error("repositories is not a table",
                                     element.source().begin.line);
        }
    }

    return result;
}

util::expected<std::string, config_error>
parse_elastic(const toml::node& input) {
    const auto tbl = input.as_table();
    if (!tbl) {
        return make_config_error("elastic configuration is not a table",
                                 input.source().begin.line);
    }

    std::optional<std::string> url{};

    // the nested loop is overkill when we only expect a single
    for (const auto& entry : *tbl) {
        std::string_view key = entry.first.str();
        const auto& value = entry.second;
        if (key == "url") {
            if (auto v = value.value<std::string>()) {
                url = v.value();
            } else {
                return make_config_error("elastic.url must be a string",
                                         value.source().begin.line);
            }
        } else {
            return make_config_error(fmt::format("unexpected key '{}'", key),
                                     value.source().begin.line);
        }
    }
    if (!url) {
        return make_config_error("elastic.url is not defined",
                                 input.source().begin.line);
    }

    return url.value();
}

util::expected<config_base, config_error>
parse_config_toml(const toml::table& input, const envvars::state& calling_env) {
    // build a config from the key value
    config_base config;

    for (const auto& entry : input) {
        std::string_view key = entry.first.str();
        const auto& value = entry.second;
        if (key == "color") {
            if (auto v = value.value<bool>()) {
                spdlog::debug("parse_config_yaml: added color {}", v.value());
                config.color = v;
            } else {
                return make_config_error(
                    "color field must be boolean (true/false)",
                    value.source().begin.line);
            }
        } else if (key == "elastic") {
            if (auto v = parse_elastic(value)) {
                spdlog::debug("parse_config_yaml: added elastic {}", v.value());
                config.elastic_config = v.value();
            } else {
                return util::unexpected{v.error()};
            }
        } else if (key == "repositories") {
            if (const auto v = parse_repository_array(value, calling_env)) {
                spdlog::debug("parse_config_yaml: added repo {}",
                              *(v.value().begin()));
                // fmt::join(v.value(), ","));
                config.repos = v.value();
            } else {
                return util::unexpected{v.error()};
            }
        } else {
            const auto error = fmt::format("unexpected key '{}'", key);
            spdlog::error("parse_config_toml: {}", error);
            return make_config_error(error, value.source().begin.line);
        }
    }

    return config;
}

util::expected<config_base, std::string>
read_config_file(const std::filesystem::path& path,
                 [[maybe_unused]] const envvars::state& calling_env) {
    namespace fs = std::filesystem;

    if (!fs::exists(path) || !std::filesystem::is_regular_file(path)) {
        return util::unexpected{"file does not exist or is not a regular file"};
    }

    const auto result = toml::parse_file(path.string());
    if (!result) {
        spdlog::warn("read_config_file: error parsing {}: {}", path.string(),
                     result.error().description());
        return util::unexpected(
            fmt::format("unable to open configuration file {}: {}",
                        path.string(), result.error().description()));
    }

    const auto config = parse_config_toml(result.table(), calling_env);

    if (!config) {
        return util::unexpected{
            fmt::format("{}: {}", path.string(), config.error())};
    }
    return config.value();
}

} // namespace impl::v2

} // namespace uenv
