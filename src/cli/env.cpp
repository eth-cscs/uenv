#include <string>
#include <unordered_map>

#include <uenv/env.h>
#include <uenv/envvars.h>

namespace uenv {

std::unordered_map<std::string, std::string> getenv(const env& environment) {
    // accumulator for the environment variables that will be set.
    // (key, value) -> (environment variable name, value)
    std::unordered_map<std::string, std::string> env_vars;

    // returns the value of an environment variable.
    // if the variable has been recorded in env_vars, that value is returned
    // else the cstdlib getenv function is called to get the currently set value
    // returns nullptr if the variable is not set anywhere
    auto ge = [&env_vars](const std::string& name) -> const char* {
        if (env_vars.count(name)) {
            return env_vars[name].c_str();
        }
        return ::getenv(name.c_str());
    };

    // iterate over each view in order, and set the environment variables that
    // each view configures.
    // the variables are not set directly, instead they are accumulated in
    // env_vars.
    for (auto& view : environment.views) {
        auto result = environment.uenvs.at(view.uenv)
                          .views.at(view.name)
                          .environment.get_values(ge);
        for (const auto& v : result) {
            env_vars[v.name] = v.value;
        }
    }

    return env_vars;
}

util::expected<int, std::string>
setenv(const std::unordered_map<std::string, std::string>& variables) {
    for (auto var : variables) {
        std::string fwd_name = "SQFSMNT_FWD_" + var.first;
        if (auto rcode = ::setenv(fwd_name.c_str(), var.second.c_str(), true)) {
            switch (rcode) {
            case EINVAL:
                return util::unexpected(
                    fmt::format("invalid variable name {}", fwd_name));
            case ENOMEM:
                return util::unexpected("out of memory");
            default:
                return util::unexpected(
                    fmt::format("unknown error setting {}", fwd_name));
            }
        }
    }
    return 0;
}

} // namespace uenv
