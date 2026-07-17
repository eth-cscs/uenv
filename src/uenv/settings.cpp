#include <filesystem>
#include <fstream>
#include <optional>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>
#include <toml++/toml.hpp>

#include <site/site.h>
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

// Parse a url-valued configuration key. The scheme defaults to https, which is
// how the CSCS deployment writes registry.url ("jfrog.svc.cscs.ch/uenv"); an
// explicit http:// is honoured so a local test registry can be reached. Any
// other scheme is refused here, at the boundary, rather than by whatever
// eventually tries to fetch it.
util::expected<util::url, config_error>
parse_config_url(std::string_view key, const std::string& text,
                 std::uint32_t line) {
    auto u = util::parse_url(text);
    if (u && u->scheme() == util::url_scheme::none) {
        // no scheme: assume https. re-parsing cannot fail - the text already
        // parsed, and a scheme is all that is being added.
        u = util::parse_url("https://" + text);
    }
    if (!u) {
        return make_config_error(
            fmt::format("{} is not a valid url: {}", key, u.error().message()),
            line);
    }
    if (u->scheme() != util::url_scheme::https &&
        u->scheme() != util::url_scheme::http) {
        return make_config_error(
            fmt::format("{} must be an http or https url, but names '{}'", key,
                        u->scheme_text()),
            line);
    }
    return *u;
}

util::expected<repository, std::string>
concretise_user_repo(const configuration& config) {
    const auto description = config.user_repo();
    if (!description) {
        return util::unexpected{
            "there is no repo specified. Add one with the --repo "
            "flag or configuration file."};
    }

    return create_repository(description->path, repo_create_mode::existsokay);
}

std::optional<uenv::repo_description> configuration::user_repo() const {
    if (default_repo) {
        return default_repo;
    }
    if (!repos.empty()) {
        return repos[0];
    }
    return std::nullopt;
}

// merge two config_base items
// lhs is the more-specific layer and takes priority.
// For repos: start from rhs, accumulate lhs so lhs wins conflicts and
// equal-priority lhs entries sort before rhs entries.
// For scalar optionals: lhs wins (first set value wins).
config_base merge(const config_base& lhs, const config_base& rhs) {
    config_base result = rhs;
    result.repos.accumulate(lhs.repos);
    result.color = lhs.color ? lhs.color : rhs.color ? rhs.color : std::nullopt;
    result.elastic_config = lhs.elastic_config   ? lhs.elastic_config
                            : rhs.elastic_config ? rhs.elastic_config
                                                 : std::nullopt;
    result.system_name = lhs.system_name   ? lhs.system_name
                         : rhs.system_name ? rhs.system_name
                                           : std::nullopt;
    result.registry = lhs.registry   ? lhs.registry
                      : rhs.registry ? rhs.registry
                                     : std::nullopt;
    result.warnings.insert(result.warnings.end(), lhs.warnings.begin(),
                           lhs.warnings.end());
    return result;
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

    config_base cfg;
    cfg.color = color::default_color(env);
    if (rexist || ravail) {
        // priority of the default repo is default_priority-1
        // which will give it higher priority than other repos with default
        // priority
        cfg.repos.accumulate(std::vector<repo_description>{
            {.name = "default",
             .path = (rexist ? *rexist : *ravail),
             .priority = repo_description::default_priority - 1}});
    }
    cfg.system_name = site::get_system_name(env);
    return cfg;
}

configuration generate_configuration(const config_base& base) {
    using enum repo_state;

    configuration config;

    // the first step is to record whether the highest priority input repository
    // does not exist.
    // this information is used later to decide whether to create the repository
    // before operations that modify repositories like `image pull` - in effect
    // automatically creating repositories for users. this is a little awkward,
    // however users find having to explicitly create a repository, particularly
    // the default repository, before they can start using it confusing and
    // inconvenient.
    if (base.repos.size() &&
        uenv::validate_repository(base.repos[0].path) == no_exist) {
        config.default_repo = base.repos[0];
    }

    // filter the list of repos to remove repos that do not exist or are in an
    // invalid state.
    config.repos = base.repos;
    config.repos.filter([](const auto& r) {
        const auto status = uenv::validate_repository(r.path);
        switch (status) {
        case readwrite:
        case readonly:
            return true;
        default:
            spdlog::warn("ignoring repository {} (invalid)", r);
            return false;
        }
    });

    // disable color output if it has not be enabled/disabled
    config.color = base.color.value_or(false);

    // set elastic logging end point
    config.elastic_config = base.elastic_config;

    // set the system name
    if (base.system_name) {
        if (parse_cluster_name(base.system_name.value())) {
            config.system_name = base.system_name;
        } else {
            spdlog::warn("generate_configuration: invalid system name '{}', "
                         "ignoring",
                         base.system_name.value());
        }
    }

    config.registry = base.registry;

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

std::optional<std::filesystem::path>
user_config_path(const envvars::state& calling_env) {
    namespace fs = std::filesystem;

    const auto home_env = calling_env.get("HOME");
    const auto xdg_env = calling_env.get("XDG_CONFIG_HOME");
    // return an null if no path available
    if (!home_env && !xdg_env) {
        return {};
    }

    const auto config_path =
        xdg_env ? (fs::path(xdg_env.value()) / "uenv")
                : (fs::path(home_env.value()) / ".config/uenv");
    const auto config_file = config_path / "config.toml";

    if (fs::exists(config_file)) {
        return config_file;
    }

    return {};
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

std::optional<std::filesystem::path> system_config_path() {
    namespace fs = std::filesystem;
    const auto base = fs::path{"/etc/uenv"};
    if (auto p = base / "config.toml"; fs::exists(p)) {
        return p;
    }
    if (auto p = base / "config"; fs::exists(p)) {
        return p;
    }
    return std::nullopt;
}

// read configuration from /etc
util::expected<config_base, std::string>
load_system_config(const envvars::state& calling_env) {
    if (const auto config_path = system_config_path(); !config_path) {
        spdlog::info("load_system_config:: no system config");
        return config_base{};
    } else if (config_path->extension() == ".toml") {
        spdlog::debug("load_system_config:: loading {}", config_path.value());
        auto result =
            impl::v2::read_config_file(config_path.value(), calling_env);
        if (!result) {
            return util::unexpected{fmt::format(
                "load_system_config::error reading config {}", result.error())};
        }
        spdlog::info("load_system_config:: loaded {}", config_path.value());
        return result;
    } else {
        spdlog::debug("load_system_config:: loading v1 configuration in {}",
                      config_path.value());
        auto result =
            impl::v1::read_config_file(config_path.value(), calling_env);
        if (!result) {
            return util::unexpected{fmt::format(
                "load_system_config::error reading config {}", result.error())};
        }
        spdlog::info("load_system_config:: loaded {}", config_path.value());
        return result;
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
        // do not treat a broken system configuration as a hard error:
        // users cannot fix system config, and a parse error must not
        // disable the tool for them.
        spdlog::error("load_config:: error reading system config file: {}",
                      sys.error());
    }

    if (auto usr = uenv::load_user_config(calling_env)) {
        config = merge(*usr, config);
    } else {
        // do not treat broken user configuration as a hard error.
        // we could make this a hard error, because users can fix their
        // configuration.
        spdlog::warn("load_config:: did not load user config: {}", usr.error());
        config.warnings.push_back(fmt::format("{}", usr.error()));
    }

    if (repos) {
        // replace errors are hard errors because this implies that the
        // user has explicitly requested an invalid repository list
        if (auto result = config.repos.replace(*repos); !result) {
            return util::unexpected{result.error()};
        }

        spdlog::info("load_config using repositories: {}",
                     fmt::join(config.repos, ", "));

        return merge(cli_config, config);
    }

    spdlog::info("load_config using repositories: {}",
                 fmt::join(config.repos, ", "));

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
            config.repos.accumulate(std::vector<repo_description>{
                {.name = "default",
                 .path = std::filesystem::path(calling_env.expand(
                     value, envvars::expand_delim::curly))}});
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
    if (!arr) {
        return make_config_error("repositories is not an array",
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
            result.push_back(
                {.name = std::move(name.value()),
                 .path = std::filesystem::path(std::move(path.value())),
                 .priority = priority});
        } else {
            return make_config_error("repositories is not a table",
                                     element.source().begin.line);
        }
    }

    return result;
}

util::expected<std::optional<std::string>, config_error>
parse_system(const toml::node& input) {
    if (const auto v = input.value<std::string>()) {
        if (const auto name = uenv::parse_cluster_name(v.value())) {
            // return the raw representation, which might be "*"
            // the value will be parsed again during generate_configuration()
            return v;
        }
    }

    return make_config_error("not a valid system name",
                             input.source().begin.line);
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

util::expected<registry_config, config_error>
parse_registry(const toml::node& input) {
    const auto tbl = input.as_table();
    if (!tbl) {
        return make_config_error("registry configuration is not a table",
                                 input.source().begin.line);
    }

    std::optional<std::string> url{};
    std::optional<std::string> default_namespace{};
    std::optional<std::string> artifactory_url{};
    std::optional<std::string> listing_url{};
    std::uint32_t url_line = input.source().begin.line;
    std::uint32_t artifactory_line = url_line;
    std::uint32_t listing_line = url_line;

    for (const auto& entry : *tbl) {
        std::string_view key = entry.first.str();
        const auto& value = entry.second;
        if (key == "url") {
            if (auto v = value.value<std::string>()) {
                url = v.value();
                url_line = value.source().begin.line;
            } else {
                return make_config_error("registry.url must be a string",
                                         value.source().begin.line);
            }
        } else if (key == "listing_url") {
            if (auto v = value.value<std::string>()) {
                listing_url = v.value();
                listing_line = value.source().begin.line;
            } else {
                return make_config_error(
                    "registry.listing_url must be a string",
                    value.source().begin.line);
            }
        } else if (key == "default_namespace") {
            if (auto v = value.value<std::string>()) {
                default_namespace = v.value();
            } else {
                return make_config_error(
                    "registry.default_namespace must be a string",
                    value.source().begin.line);
            }
        } else if (key == "artifactory_url") {
            if (auto v = value.value<std::string>()) {
                artifactory_url = v.value();
                artifactory_line = value.source().begin.line;
            } else {
                return make_config_error(
                    "registry.artifactory_url must be a string",
                    value.source().begin.line);
            }
        } else {
            return make_config_error(fmt::format("unexpected key '{}'", key),
                                     value.source().begin.line);
        }
    }
    if (!url) {
        return make_config_error("registry.url is not defined",
                                 input.source().begin.line);
    }
    if (!default_namespace) {
        return make_config_error("registry.default_namespace is not defined",
                                 input.source().begin.line);
    }

    // parse the endpoints here, at the boundary, so a malformed url is reported
    // against the key that carries it.
    auto parsed_url = parse_config_url("registry.url", *url, url_line);
    if (!parsed_url) {
        return util::unexpected{parsed_url.error()};
    }
    std::optional<util::url> parsed_artifactory;
    if (artifactory_url) {
        auto p = parse_config_url("registry.artifactory_url", *artifactory_url,
                                  artifactory_line);
        if (!p) {
            return util::unexpected{p.error()};
        }
        parsed_artifactory = std::move(*p);
    }
    std::optional<util::url> parsed_listing;
    if (listing_url) {
        auto p = parse_config_url("registry.listing_url", *listing_url,
                                  listing_line);
        if (!p) {
            return util::unexpected{p.error()};
        }
        parsed_listing = std::move(*p);
    }

    return registry_config{.url = std::move(*parsed_url),
                           .default_namespace =
                               std::move(default_namespace.value()),
                           .artifactory_url = std::move(parsed_artifactory),
                           .listing_url = std::move(parsed_listing)};
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
                spdlog::debug("parse_config_toml: added color {}", v.value());
                config.color = v;
            } else {
                return make_config_error(
                    "color field must be boolean (true/false)",
                    value.source().begin.line);
            }
        } else if (key == "elastic") {
            if (auto v = parse_elastic(value)) {
                spdlog::debug("parse_config_toml: added elastic {}", v.value());
                config.elastic_config = v.value();
            } else {
                return util::unexpected{v.error()};
            }
        } else if (key == "system_name") {
            if (auto v = parse_system(value)) {
                spdlog::debug("parse_config_toml: added system {}", v.value());
                config.system_name = v.value();
            } else {
                return util::unexpected{v.error()};
            }
        } else if (key == "repositories") {
            if (const auto v = parse_repository_array(value, calling_env)) {
                spdlog::debug("parse_config_toml: added repo {}",
                              *(v.value().begin()));
                config.repos.accumulate(v.value());
            } else {
                return util::unexpected{v.error()};
            }
        } else if (key == "registry") {
            if (auto v = parse_registry(value)) {
                spdlog::debug("parse_config_toml: added registry url {}",
                              v.value().url);
                config.registry = std::move(v.value());
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
        spdlog::warn("read_config_file:: {} {}", path.string(),
                     result.error().description());
        const auto pos = result.error().source().begin;
        return util::unexpected(fmt::format("(line {} col {}) {}", pos.line,
                                            pos.column,
                                            result.error().description()));
    }

    const auto config = parse_config_toml(result.table(), calling_env);

    if (!config) {
        return util::unexpected{fmt::format("{}", config.error())};
    }
    return config.value();
}

} // namespace impl::v2

} // namespace uenv
