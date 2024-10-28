#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <fmt/core.h>
#include <fmt/std.h>

namespace uenv {

struct scalar {
    std::string name;
    std::string value;
};

bool operator==(const scalar& lhs, const scalar& rhs);

enum class update_kind { set, prepend, append };

struct prefix_path_update {
    void apply(std::vector<std::string>&);

    update_kind op;
    std::vector<std::string> values;
};

struct prefix_path {
    prefix_path(std::string name) : name_(std::move(name)) {};
    prefix_path() = delete;

    void update(prefix_path_update u);
    std::string get(const std::string& initial_value = "") const;

    std::string_view name() const {
        return name_;
    }
    const std::vector<prefix_path_update>& updates() const {
        return updates_;
    }

  private:
    std::string name_;
    std::vector<prefix_path_update> updates_;
};

struct envvarset {
    bool update_scalar(const std::string& name, const std::string& value);
    bool update_prefix_path(const std::string& name, prefix_path_update update);
    std::vector<scalar>
    get_values(std::function<const char*(const std::string&)> f) const;

    const std::unordered_map<std::string, scalar>& scalars() const {
        return scalars_;
    };
    const std::unordered_map<std::string, prefix_path>& prefix_paths() const {
        return prefix_paths_;
    };

  private:
    std::unordered_map<std::string, scalar> scalars_;
    std::unordered_map<std::string, prefix_path> prefix_paths_;
};

std::vector<std::string>
simplify_prefix_path_list(const std::vector<std::string>& in);

} // namespace uenv

#include "fmt/envvars.fmt"
