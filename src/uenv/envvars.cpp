#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <util/strings.h>

#include "envvars.h"

namespace uenv {

//
// scalar implementation
//

bool operator==(const scalar& lhs, const scalar& rhs) {
    return lhs.name == rhs.name && lhs.value == rhs.value;
}

void scalar::expand_env_variables(const environment::variables& env) {
    value = env.expand(value, environment::expand_delim::view);
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

void prefix_path::expand_env_variables(const environment::variables& env) {
    for (auto& u : updates_) {
        for (auto v : u.values) {
            v = env.expand(v, environment::expand_delim::view);
        }
    }
}

//
// envvarset implementation
//

bool envvarset::update_scalar(const std::string& name,
                              const std::string& value) {
    bool conflict = false;
    if (prefix_paths_.count(name)) {
        prefix_paths_.erase(name);
        conflict = true;
    }
    scalars_[name] = {name, value};
    return conflict;
}

bool envvarset::update_prefix_path(const std::string& name,
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

std::vector<scalar> envvarset::get_values(
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
void envvarset::expand_env_variables(const environment::variables& env) {
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

} // namespace uenv
