#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace environment {

// used to indicate the method used to embed environment variable names in
// strings for expansion
//
// curly:  variables are delimited with '${' and '}', e.g:
//      "${CUDA_HOME}"
//      "${SCRATCH}/local-files"
//      "${HOME}/.config/${CLUSTER_NAME}"
//
// view:   used in env.json view definitions, to avoid conflicts.
//         variables are delimited with '${@' and '@}', e.g:
//      "${@CUDA_HOME@}"
//      "${@SCRATCH@}/local-files"
//      "${@HOME@}/.config/${@CLUSTER_NAME@}"
enum class expand_delim { curly, view };

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

    std::string expand(std::string_view, expand_delim) const;

    char** c_env() const;

  private:
    std::unordered_map<std::string, std::string> variables_;
};

void c_env_free(char** env);

} // namespace environment
