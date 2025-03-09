#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace environment {

class variables {
  public:
    variables() = default;
    variables(char* env[]);
    variables(const variables&) = default;
    void set(std::string_view name, std::string_view value);
    void unset(std::string_view name);
    void clear();

    const std::unordered_map<std::string, std::string> vars() const;

    std::optional<std::string> get(std::string_view name) const;

    char** c_env() const;

  private:
    std::unordered_map<std::string, std::string> variables_;
};

void c_env_free(char** env);

} // namespace environment
