#include <unistd.h>

#include <cctype>

#include <string>
#include <string_view>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "environment.h"

namespace environment {

// Environment variable names can contain any character from the portable
// character set:
//   https://pubs.opengroup.org/onlinepubs/009695099/basedefs/xbd_chap06.html
// except NUL and '=':
//   https://pubs.opengroup.org/onlinepubs/000095399/basedefs/xbd_chap08.html
// in practice, shells and utilities stick to [a-zA-Z_][a-zA-Z0-9_]*
//
// Provide two modes:
//  strict==true  : [a-zA-Z_][a-zA-Z0-9_]*
//  strict==false : no '='
bool validate_name(std::string_view name, bool strict = true) {
    if (name.empty()) {
        return false;
    }

    if (!strict) {
        return name.find('=') == std::string_view::npos;
    }

    // environment variable names can't start with a digit
    if (std::isdigit(name[0])) {
        return false;
    }

    // assert that characters are characters, digits or underscore
    for (unsigned char c : name) {
        if (!(std::isalnum(c) || c == '_')) {
            return false;
        }
    }

    return true;
}

variables::variables(char* environ[]) {
    if (environ == nullptr) {
        return;
    }
    for (char** env = environ; *env != nullptr; ++env) {
        const std::string entry(*env);
        if (const auto pos = entry.find('='); pos != std::string::npos) {
            const auto name = entry.substr(0, pos);
            const auto value = entry.substr(pos + 1);
            // Apply weak validation to variable names read from the global set
            // of environment variables in order to follow the directive
            // 'applications shall tolerate the presence of such names.' from
            // the standard:
            //   https://pubs.opengroup.org/onlinepubs/000095399/basedefs/xbd_chap08.html
            if (validate_name(name, false)) {
                variables_[name] = value;
                spdlog::trace("environment::variables init {}='{}'", name,
                              value);
            } else {
                spdlog::warn("environment::variables skipping the invalid "
                             "environment variable name '{}'",
                             name);
            }
        }
    }
}

void variables::set(const std::string_view name, std::string_view value) {
    if (validate_name(name)) {
        variables_[std::string(name)] = value;
    } else {
        spdlog::warn("environment::variables::set skipping the invalid "
                     "environment variable name '{}'",
                     name);
    }
}

std::optional<std::string> variables::get(std::string_view name) const {
    if (validate_name(name)) {
        auto it = variables_.find(std::string(name));
        if (it != variables_.end()) {
            return it->second;
        }
    } else {
        spdlog::warn("environment::variables::get invalid environment variable "
                     "name '{}'",
                     name);
    }
    return std::nullopt;
}

void variables::unset(std::string_view name) {
    if (validate_name(name)) {
        variables_.erase(std::string(name));
    } else {
        spdlog::warn(
            "environment::variables::unset invalid environment variable "
            "name '{}'",
            name);
    }
}

const std::unordered_map<std::string, std::string> variables::vars() const {
    return variables_;
}

char** variables::c_env() const {
    const auto n = variables_.size();
    char** ev = reinterpret_cast<char**>(malloc((n + 1) * sizeof(char*)));
    spdlog::info("environment::variables::c_env using {} bytes",
                 (n + 1) * sizeof(char*));
    ev[n] = nullptr;
    unsigned i = 0;
    spdlog::info("environment::variables::c_env outputing {} variables", n);
    for (auto& p : variables_) {
        // generate a string: name=value
        // length is len(name)+len(value)+2 ('=' and '\0')
        const auto len = p.first.size() + p.second.size() + 2;
        ev[i] = (char*)malloc(len * sizeof(char));
        fmt::format_to_n(ev[i], len - 1, "{}={}", p.first, p.second);
        ev[i][len - 1] = 0;
        ++i;
    }

    return ev;
}

void variables::clear() {
    variables_.clear();
}

void c_env_cleanup(char** env) {
    if (env == nullptr) {
        return;
    }
    auto p = env;
    while (*p) {
        free(*p);
        ++p;
    }
    free(env);
}

} // namespace environment
