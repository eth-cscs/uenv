#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

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
    void update(prefix_path_update u);
    std::string get(const std::string& initial_value = "") const;

  private:
    std::string name;
    std::vector<prefix_path_update> updates;
};

struct envvarset {
    bool update_scalar(const std::string& name, const std::string& value);
    bool update_prefix_path(const std::string& name, prefix_path_update update);
    std::vector<scalar>
    get_values(std::function<const char*(const std::string&)> f) const;

  private:
    std::unordered_map<std::string, scalar> scalars;
    std::unordered_map<std::string, prefix_path> prefix_paths;
};

std::vector<std::string>
simplify_prefix_path_list(const std::vector<std::string>& in);

} // namespace uenv
