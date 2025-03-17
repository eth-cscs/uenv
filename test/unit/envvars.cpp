#include <catch2/catch_all.hpp>

#include <vector>

// #include <uenv/envvars.h>
#include <util/envvars.h>

TEST_CASE("prefix_path update", "[envvars]") {
    envvars::prefix_path_update pr_empty{envvars::update_kind::prepend, {}};
    envvars::prefix_path_update pr_2{envvars::update_kind::prepend, {"a", "b"}};
    envvars::prefix_path_update set_empty{envvars::update_kind::set, {}};
    envvars::prefix_path_update set_2{envvars::update_kind::set, {"c", "d"}};
    envvars::prefix_path_update ap_empty{envvars::update_kind::append, {}};
    envvars::prefix_path_update ap_2{envvars::update_kind::append, {"e", "f"}};
    envvars::prefix_path_update unset{envvars::update_kind::unset, {}};

    {
        std::vector<std::string> value = {};
        bool is_set = false;

        set_2.apply(value, is_set);
        REQUIRE_THAT(
            value, Catch::Matchers::Equals(std::vector<std::string>{"c", "d"}));

        // check that applying unset works
        unset.apply(value, is_set);
        REQUIRE(!is_set);

        // setting an unset variable will reset the variable
        set_2.apply(value, is_set);
        REQUIRE_THAT(
            value, Catch::Matchers::Equals(std::vector<std::string>{"c", "d"}));

        set_empty.apply(value, is_set);
        REQUIRE_THAT(value,
                     Catch::Matchers::Equals(std::vector<std::string>{}));
        REQUIRE(is_set);

        // check that applying unset works
        unset.apply(value, is_set);
        REQUIRE(!is_set);
    }
    {
        std::vector<std::string> value = {};
        bool is_set = false;
        set_2.apply(value, is_set);
        REQUIRE_THAT(
            value, Catch::Matchers::Equals(std::vector<std::string>{"c", "d"}));
        pr_2.apply(value, is_set);
        REQUIRE_THAT(value, Catch::Matchers::Equals(
                                std::vector<std::string>{"a", "b", "c", "d"}));
        pr_empty.apply(value, is_set);
        REQUIRE_THAT(value, Catch::Matchers::Equals(
                                std::vector<std::string>{"a", "b", "c", "d"}));
        ap_empty.apply(value, is_set);
        REQUIRE_THAT(value, Catch::Matchers::Equals(
                                std::vector<std::string>{"a", "b", "c", "d"}));
        ap_2.apply(value, is_set);
        REQUIRE_THAT(value, Catch::Matchers::Equals(std::vector<std::string>{
                                "a", "b", "c", "d", "e", "f"}));
        REQUIRE(is_set);
    }
}

TEST_CASE("prefix_path simplify", "[envvars]") {
    REQUIRE_THAT(envvars::simplify_prefix_path_list({}),
                 Catch::Matchers::Equals(std::vector<std::string>{}));

    REQUIRE_THAT(envvars::simplify_prefix_path_list({"a"}),
                 Catch::Matchers::Equals(std::vector<std::string>{"a"}));

    REQUIRE_THAT(envvars::simplify_prefix_path_list({"a", "a"}),
                 Catch::Matchers::Equals(std::vector<std::string>{"a"}));

    REQUIRE_THAT(envvars::simplify_prefix_path_list({"a", "b", "a"}),
                 Catch::Matchers::Equals(std::vector<std::string>{"a", "b"}));

    REQUIRE_THAT(envvars::simplify_prefix_path_list({"c", "d"}),
                 Catch::Matchers::Equals(std::vector<std::string>{"c", "d"}));

    REQUIRE_THAT(envvars::simplify_prefix_path_list(
                     {"z", "hello", "apple", "cat", "apple", "z", "wombat"}),
                 Catch::Matchers::Equals(std::vector<std::string>{
                     "z", "hello", "apple", "cat", "wombat"}));
    REQUIRE_THAT(envvars::simplify_prefix_path_list(
                     {"/opt/cray/libfabric/1.15.2.0", "/usr/lib64",
                      "/opt/cray/libfabric/1.15.2.0"}),
                 Catch::Matchers::Equals(std::vector<std::string>{
                     "/opt/cray/libfabric/1.15.2.0", "/usr/lib64"}));
}

// check that conflicts are correctly flagged
TEST_CASE("patch validate", "[envvars]") {
    envvars::patch ev;
    REQUIRE_FALSE(ev.update_scalar("FOO", "wombat"));
    REQUIRE(
        ev.update_prefix_path("FOO", {envvars::update_kind::set, {"wombat"}}));
    REQUIRE_FALSE(ev.update_prefix_path("apple", {{}, {}}));
    REQUIRE(ev.update_scalar("apple", ""));
}

// check environment variable substitution
TEST_CASE("patch expand", "[envvars]") {
    envvars::state vars;
    vars.set("SCRATCH", "/scratch/cscs/wombat");
    vars.set("CLUSTER_NAME", "daint");

    envvars::patch patch;
    patch.update_scalar("JULIA_HOME",
                        "${@SCRATCH@}/julia-up/${@CLUSTER_NAME@}");

    vars.apply_patch(patch, envvars::expand_delim::view);

    REQUIRE(vars.get("JULIA_HOME"));
    REQUIRE(vars.get("JULIA_HOME").value() ==
            "/scratch/cscs/wombat/julia-up/daint");
}

// check that final patch variables are correctly generated
TEST_CASE("patch get final values", "[envvars]") {
    envvars::patch ev;
    ev.update_scalar("ANIMAL", "wombat");
    ev.update_scalar("VEHICLE", "car");
    ev.update_scalar("ABODE", "tent");

    ev.update_prefix_path("A", {envvars::update_kind::set, {"c", "d"}});
    ev.update_prefix_path("B", {envvars::update_kind::prepend, {"a", "b"}});
    ev.update_prefix_path("C", {envvars::update_kind::append, {"c", "d"}});

    {
        auto getenv = []([[maybe_unused]] const std::string& name)
            -> std::optional<std::string> { return std::nullopt; };
        REQUIRE_THAT(ev.get_values(getenv),
                     Catch::Matchers::UnorderedEquals(
                         std::vector<envvars::scalar>{{"ABODE", "tent"},
                                                      {"VEHICLE", "car"},
                                                      {"ANIMAL", "wombat"},
                                                      {"A", "c:d"},
                                                      {"B", "a:b"},
                                                      {"C", "c:d"}}));
    }

    {
        auto getenv =
            [](const std::string& name) -> std::optional<std::string> {
            if (name == "A")
                return "hello";
            if (name == "B")
                return "c:d";
            if (name == "C")
                return "a:b:b:a:c:a";
            return std::nullopt;
        };
        REQUIRE_THAT(ev.get_values(getenv),
                     Catch::Matchers::UnorderedEquals(
                         std::vector<envvars::scalar>{{"ABODE", "tent"},
                                                      {"VEHICLE", "car"},
                                                      {"ANIMAL", "wombat"},
                                                      {"A", "c:d"},
                                                      {"B", "a:b:c:d"},
                                                      {"C", "a:b:c:d"}}));
    }
}

/*
 * test the environment variable handler below here
 */
namespace envvars {
bool validate_name(std::string_view name, bool strict = true);
}

TEST_CASE("validate envvar names", "[environment]") {
    REQUIRE(envvars::validate_name("wombat", true));
    REQUIRE(envvars::validate_name("_", true));
    REQUIRE(envvars::validate_name("__", true));
    REQUIRE(envvars::validate_name("_WOMBAT", true));
    REQUIRE(envvars::validate_name("a", true));
    REQUIRE(envvars::validate_name("A", true));
    REQUIRE(envvars::validate_name("ab", true));
    REQUIRE(envvars::validate_name("AB", true));
    REQUIRE(envvars::validate_name("PATH", true));
    REQUIRE(envvars::validate_name("CUDA_HOME", true));
    REQUIRE(envvars::validate_name("P1", true));
    REQUIRE(envvars::validate_name("_1", true));
    REQUIRE(envvars::validate_name("a123_4", true));

    REQUIRE(!envvars::validate_name("a-b", true));
    REQUIRE(!envvars::validate_name("b?", true));
    REQUIRE(!envvars::validate_name("-", true));
    REQUIRE(!envvars::validate_name("!", true));
    REQUIRE(!envvars::validate_name("wombat soup", true));
}

TEST_CASE("state::clear", "[environment]") {
    {
        envvars::state E{};
        auto initial_vars = E.c_env();
        // assume that the calling environment has at least one environment
        // variabls
        REQUIRE(initial_vars != nullptr);
        REQUIRE(initial_vars[0] == nullptr);
        E.clear();
        auto final_vars = E.c_env();
        REQUIRE(final_vars != nullptr);
        REQUIRE(final_vars[0] == nullptr);

        envvars::c_env_free(final_vars);
        envvars::c_env_free(initial_vars);
    }
    {
        envvars::state E{environ};
        auto initial_vars = E.c_env();
        // assume that the calling environment has at least one environment
        // variabls
        REQUIRE(initial_vars != nullptr);
        REQUIRE(initial_vars[0] != nullptr);
        E.clear();
        auto final_vars = E.c_env();
        REQUIRE(final_vars != nullptr);
        REQUIRE(final_vars[0] == nullptr);

        envvars::c_env_free(final_vars);
        envvars::c_env_free(initial_vars);
    }
}

TEST_CASE("state::set-get-unset", "[environment]") {
    auto n_env = [](char** env) -> unsigned {
        if (env == nullptr) {
            return 0u;
        }
        unsigned count = 0;
        while (*env) {
            ++env;
            ++count;
        }
        return count;
    };

    envvars::state E{};

    {
        auto vars = E.c_env();
        REQUIRE(n_env(vars) == 0u);
        envvars::c_env_free(vars);
    }

    E.set("hello", "world");
    REQUIRE(E.get("hello"));
    REQUIRE(E.get("hello").value() == "world");
    {

        auto vars = E.c_env();
        REQUIRE(n_env(vars) == 1u);
        REQUIRE(vars[0] == std::string("hello=world"));
        envvars::c_env_free(vars);
    }
    E.set("hello", "there");
    REQUIRE(E.get("hello"));
    REQUIRE(E.get("hello").value() == "there");
    {

        auto vars = E.c_env();
        REQUIRE(n_env(vars) == 1u);
        REQUIRE(vars[0] == std::string("hello=there"));
        envvars::c_env_free(vars);
    }
    E.set("another", "variable");
    REQUIRE(E.get("another"));
    REQUIRE(E.get("another").value() == "variable");
    {
        auto vars = E.c_env();
        REQUIRE(n_env(vars) == 2u);
        envvars::c_env_free(vars);
    }
    E.set("and", "another");
    {
        auto vars = E.c_env();
        REQUIRE(n_env(vars) == 3u);
        envvars::c_env_free(vars);
    }
    E.set("and", "overwrite");
    {
        auto vars = E.c_env();
        REQUIRE(n_env(vars) == 3u);
        envvars::c_env_free(vars);
    }
    // an illegal environment variable name will be ignored (a warning log
    // message is generated)
    E.set("and it was always thus", "antechinus");
    {
        auto vars = E.c_env();
        REQUIRE(n_env(vars) == 3u);
        envvars::c_env_free(vars);
    }
    REQUIRE(!E.get("and it was always thus"));

    REQUIRE(!E.get("A_VALID_NAME"));
    REQUIRE(!E.get("_"));

    // set a variable, then check that unset removes the variable
    E.set("wombat", "soup");
    REQUIRE(E.get("wombat").value() == "soup");
    E.unset("wombat");
    REQUIRE(!E.get("wombat"));
}

TEST_CASE("state::expand-curly", "[environment]") {
    envvars::state V{};
    auto mode = envvars::expand_delim::curly;

    V.set("USER", "wombat");

    REQUIRE(V.expand("", mode) == "");
    REQUIRE(V.expand("_", mode) == "_");
    REQUIRE(V.expand("hello", mode) == "hello");
    REQUIRE(V.expand("{} aa", mode) == "{} aa");
    REQUIRE(V.expand("$$", mode) == "$$");
    REQUIRE(V.expand("$UNSET", mode) == "$UNSET");
    REQUIRE(V.expand("$", mode) == "$");
    REQUIRE(V.expand("a-$", mode) == "a-$");
    REQUIRE(V.expand("${USER}", mode) == "wombat");
    REQUIRE(V.expand("${USER}s", mode) == "wombats");
    REQUIRE(V.expand("${USER}-soup", mode) == "wombat-soup");
    REQUIRE(V.expand("${USER}${USER}", mode) == "wombatwombat");
    REQUIRE(V.expand("hello ${USER}", mode) == "hello wombat");

    V.set("greeting", "hello");
    REQUIRE(V.expand("${greeting} world", mode) == "hello world");
    REQUIRE(V.expand("I say \"${greeting}\"", mode) == "I say \"hello\"");

    // this is treated as an error by bash
    // we simply "do nothing", and log an error
    //   > bash -c 'echo ${PATH'
    //   bash: -c: line 1: unexpected EOF while looking for matching `}'
    REQUIRE(V.expand("${USER", mode) == "");

    REQUIRE(V.expand("${USER NAME}", mode) == "");
    REQUIRE(V.expand("${$happy}-there", mode) == "-there");

    V.set("CUDA_HOME", "/opt/cuda");
    REQUIRE(V.expand("/usr/lib:${CUDA_HOME}/lib:${CUDA_HOME}/lib64", mode) ==
            "/usr/lib:/opt/cuda/lib:/opt/cuda/lib64");
}

TEST_CASE("state::expand-view", "[environment]") {
    envvars::state V{};
    auto mode = envvars::expand_delim::view;

    V.set("USER", "wombat");

    REQUIRE(V.expand("", mode) == "");
    REQUIRE(V.expand("_", mode) == "_");
    REQUIRE(V.expand("hello", mode) == "hello");
    REQUIRE(V.expand("{} aa", mode) == "{} aa");
    REQUIRE(V.expand("$$", mode) == "$$");
    REQUIRE(V.expand("$UNSET", mode) == "$UNSET");
    REQUIRE(V.expand("$", mode) == "$");
    REQUIRE(V.expand("a-$", mode) == "a-$");
    REQUIRE(V.expand("${USER}", mode) == "${USER}");
    REQUIRE(V.expand("${@USER@}", mode) == "wombat");
    REQUIRE(V.expand("${@USER@}s", mode) == "wombats");
    REQUIRE(V.expand("${@USER@}-soup", mode) == "wombat-soup");
    REQUIRE(V.expand("${@USER@}${@USER@}", mode) == "wombatwombat");
    REQUIRE(V.expand("hello ${@USER@}", mode) == "hello wombat");
    REQUIRE(V.expand("hello ${@USER@} and welcome", mode) ==
            "hello wombat and welcome");

    V.set("greeting", "hello");
    REQUIRE(V.expand("${@greeting@} world", mode) == "hello world");
    REQUIRE(V.expand("I say \"${@greeting@}\"", mode) == "I say \"hello\"");

    // this is treated as an error by bash
    // we simply "do nothing", and log an error
    //   > bash -c 'echo ${PATH'
    //   bash: -c: line 1: unexpected EOF while looking for matching `}'
    REQUIRE(V.expand("${@USER", mode) == "");
    REQUIRE(V.expand("${@USER}", mode) == "");

    REQUIRE(V.expand("${@USER NAME@}", mode) == "");
    REQUIRE(V.expand("${@$happy@}-there", mode) == "-there");

    V.set("CUDA_HOME", "/opt/cuda");
    REQUIRE(V.expand("/usr/lib:${@CUDA_HOME@}/lib:${@CUDA_HOME@}/lib64",
                     mode) == "/usr/lib:/opt/cuda/lib:/opt/cuda/lib64");
}
