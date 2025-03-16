#include <unistd.h>

#include <cctype>

#include <set>
#include <string>
#include <string_view>

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <util/envvars.h>
#include <util/strings.h>

namespace envvars {

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

state::state(char* environ[]) {
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
                spdlog::trace("envvars::state init {}='{}'", name, value);
            } else {
                spdlog::warn("envvars::state skipping the invalid "
                             "environment variable name '{}'",
                             name);
            }
        }
    }
}

void state::set(const std::string_view name, std::string_view value) {
    if (validate_name(name)) {
        variables_[std::string(name)] = value;
    } else {
        spdlog::warn("envvars::state::set skipping the invalid "
                     "environment variable name '{}'",
                     name);
    }
}

std::optional<std::string> state::get(std::string_view name) const {
    if (validate_name(name)) {
        auto it = variables_.find(std::string(name));
        if (it != variables_.end()) {
            return it->second;
        }
    } else {
        spdlog::warn("envvars::state::get invalid environment variable "
                     "name '{}'",
                     name);
    }
    return std::nullopt;
}

void state::unset(std::string_view name) {
    if (validate_name(name)) {
        variables_.erase(std::string(name));
    } else {
        spdlog::warn("envvars::state::unset invalid environment variable "
                     "name '{}'",
                     name);
    }
}

const std::unordered_map<std::string, std::string> state::variables() const {
    return variables_;
}

char** state::c_env() const {
    const auto n = variables_.size();
    char** ev = reinterpret_cast<char**>(malloc((n + 1) * sizeof(char*)));
    spdlog::info("envvars::state::c_env using {} bytes",
                 (n + 1) * sizeof(char*));
    ev[n] = nullptr;
    unsigned i = 0;
    spdlog::info("envvars::state::c_env outputing {} variables", n);
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

void state::clear() {
    variables_.clear();
}

void c_env_free(char** env) {
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

namespace impl {
std::string expand(std::string_view in, expand_delim mode, const state& vars);
}

std::string state::expand(std::string_view src, expand_delim mode) const {
    const auto expanded = impl::expand(src, mode, *this);
    spdlog::trace("envvars::state::expand '{}' -> '{}'", src, expanded);
    return expanded;
}

namespace impl {

struct parser {
    using iterator = std::string_view::const_iterator;

    iterator b;
    iterator e;
    iterator current;
    expand_delim mode;

    parser() = delete;
    parser(const std::string_view str, expand_delim mode = expand_delim::curly)
        : b(std::begin(str)), e(std::end(str)), current(std::begin(str)),
          mode(mode) {
    }
    parser(const parser& other) = delete;
    // parser(const parser& other) = default;

    // process from the current string up to the next delimiter or end of
    // string, whichever is first.
    // return the text from the current position up to where we advanced.
    auto to_delim() {
        auto start = current;
        while (current != e && !start_delim()) {
            ++current;
        }
        return std::string_view{start, current};
    }

    std::optional<std::string_view> parse_name() {
        // eat the ${ or ${@
        current += (mode == expand_delim::curly) ? 2 : 3;

        // start is the first character of the name
        auto start = current;
        // advance until we hit the closing } or @}
        while (current != e && !end_delim()) {
            ++current;
        }

        // ignore the name if we ran off the end of the input without hitting
        // the closing delimiter
        if (end_delim()) {
            std::string_view result{start, current};
            // eat the } or @}
            current += (mode == expand_delim::curly) ? 1 : 2;
            return result;
        }

        // ran off the end of the input
        return std::nullopt;
    }

    // returns true if the delimiter that marks the start of an environment
    // variable expansion starts at the current position.
    // expansion is indicated by "${" or "${@" for simple and view modes
    // respectively.
    bool start_delim() const {
        if (*current == '$') {
            switch (mode) {
            case expand_delim::curly:
                if (peek() == '{') {
                    return true;
                }
                break;
            case expand_delim::view:
                if (peek() == '{' && peek(2) == '@') {
                    return true;
                }
                break;
            }
        }
        return false;
    }

    // returns true if the delimiter that marks the end of an environment
    // variable expansion starts at the current position.
    // expansion is indicated by "}" or "@}" for simple and view modes
    // respectively.
    bool end_delim() const {
        switch (mode) {
        case expand_delim::curly:
            if (*current == '}') {
                return true;
            }
            break;
        case expand_delim::view:
            if (*current == '@' && peek() == '}') {
                return true;
            }
            break;
        }
        return false;
    }

    char peek(unsigned n = 1) const {
        auto i = current;
        while (i != e && n) {
            ++i;
            --n;
        }
        // return "end of string" if peek ran off the end of the string
        return i == e ? 0 : *i;
    }

    operator bool() const {
        return current != e;
    }
};

std::string expand(std::string_view in, expand_delim mode, const state& vars) {
    std::string result;
    result.reserve(in.size());

    auto P = parser{in, mode};

    while (P) {
        auto s = P.to_delim();
        result.insert(result.end(), s.begin(), s.end());
        if (P) {
            if (auto name = P.parse_name()) {
                // use strict name checking - don't use strange env.
                // var. names in substitutions
                if (!validate_name(*name, true)) {
                    spdlog::warn("envvars::state::expand: skipping "
                                 "invalid env var name {}",
                                 *name);
                } else {
                    if (auto value = vars.get(*name)) {
                        result += *value;
                    } else {
                        spdlog::warn("envvars::state::expand: env. "
                                     "variable {} does not exist",
                                     *name);
                    }
                }
            } else {
                spdlog::error("envvars::state::expand: unexpected end of "
                              "string while looking for matching '}}': '{}'",
                              in);
            }
        }
    }

    return result;
}

} // namespace impl

//
// scalar implementation
//

bool operator==(const scalar& lhs, const scalar& rhs) {
    return lhs.name == rhs.name && lhs.value == rhs.value;
}

void scalar::expand_env_variables(const envvars::state& env) {
    value = env.expand(value, envvars::expand_delim::view);
}

//
// prefix_path implementation
//

void prefix_path_update::apply(std::vector<std::string>& in, bool& set) {
    if (op == update_kind::set) {
        in = values;
        set = true;
    } else if (op == update_kind::append) {
        in.insert(in.end(), values.begin(), values.end());
        set = true;
    } else if (op == update_kind::prepend) {
        in.insert(in.begin(), values.begin(), values.end());
        set = true;
    } else {
        in.clear();
        set = false;
    }
}

void prefix_path::update(prefix_path_update u) {
    updates_.push_back(std::move(u));
}

std::optional<std::string>
prefix_path::get(const std::string& initial_value) const {
    auto value = util::split(initial_value, ':', true);
    bool is_set = true;
    for (auto u : updates_) {
        u.apply(value, is_set);
    }
    // if the variable has been unset:
    if (!is_set) {
        return std::nullopt;
    }
    return util::join(":", simplify_prefix_path_list(value));
}

void prefix_path::expand_env_variables(const envvars::state& env) {
    for (auto& u : updates_) {
        for (auto v : u.values) {
            v = env.expand(v, envvars::expand_delim::view);
        }
    }
}

//
// patch implementation
//

bool patch::update_scalar(const std::string& name, const std::string& value) {
    bool conflict = false;
    if (prefix_paths_.count(name)) {
        prefix_paths_.erase(name);
        conflict = true;
    }
    scalars_[name] = {name, value};
    return conflict;
}

bool patch::update_prefix_path(const std::string& name,
                               prefix_path_update update) {
    bool conflict = false;
    if (scalars_.count(name)) {
        scalars_.erase(name);
        conflict = true;
    }
    prefix_paths_.try_emplace(name, prefix_path{name});
    prefix_paths_.at(name).update(update);
    return conflict;
}

std::vector<scalar> patch::get_values(
    std::function<std::optional<std::string>(const std::string&)> getenv)
    const {
    std::vector<scalar> vars;
    vars.reserve(scalars_.size() + prefix_paths_.size());

    for (auto& v : scalars_) {
        vars.push_back(v.second);
    }

    for (auto& v : prefix_paths_) {
        if (auto current = getenv(v.first)) {
            if (auto value = v.second.get(*current)) {
                vars.push_back({v.first, *value});
            }
        } else {
            if (auto value = v.second.get()) {
                vars.push_back({v.first, *value});
            }
        }
    }

    return vars;
}

// expand any environment variables of the form "${VAR}"
void patch::expand_env_variables(const envvars::state& env) {
    for (auto& var : scalars_) {
        var.second.expand_env_variables(env);
    }
    for (auto& var : prefix_paths_) {
        var.second.expand_env_variables(env);
    }
}

// remove duplicate paths, keeping the paths in the order that they are first
// encountered.
// effectively implements std::unique for an unsorted vector of
// strings, maintaining partial ordering.
std::vector<std::string>
simplify_prefix_path_list(const std::vector<std::string>& in) {
    std::set<std::string> s;
    std::vector<std::string> out;

    out.reserve(in.size());

    for (auto& p : in) {
        // drop all empty paths, which occur when there are two ::, e.g.
        // PATH=/usr/bin::/opt/cuda/bin
        if (p.size() == 0)
            continue;
        // if the path has not already been seen, add it to the output
        if (!s.count(p)) {
            out.push_back(p);
            s.insert(p);
        }
    }

    return out;
}

} // namespace envvars
