#include <catch2/catch_all.hpp>

#include <vector>

#include <uenv/envvars.h>
#include <util/environment.h>

TEST_CASE("prefix_path update", "[envvars]") {
    uenv::prefix_path_update pr_empty{uenv::update_kind::prepend, {}};
    uenv::prefix_path_update pr_2{uenv::update_kind::prepend, {"a", "b"}};
    uenv::prefix_path_update set_empty{uenv::update_kind::set, {}};
    uenv::prefix_path_update set_2{uenv::update_kind::set, {"c", "d"}};
    uenv::prefix_path_update ap_empty{uenv::update_kind::append, {}};
    uenv::prefix_path_update ap_2{uenv::update_kind::append, {"e", "f"}};

    {
        std::vector<std::string> value = {};
        set_2.apply(value);
        REQUIRE_THAT(
            value, Catch::Matchers::Equals(std::vector<std::string>{"c", "d"}));
        set_2.apply(value);
        REQUIRE_THAT(
            value, Catch::Matchers::Equals(std::vector<std::string>{"c", "d"}));

        set_empty.apply(value);
        REQUIRE_THAT(value,
                     Catch::Matchers::Equals(std::vector<std::string>{}));
    }
    {
        std::vector<std::string> value = {};
        set_2.apply(value);
        REQUIRE_THAT(
            value, Catch::Matchers::Equals(std::vector<std::string>{"c", "d"}));
        pr_2.apply(value);
        REQUIRE_THAT(value, Catch::Matchers::Equals(
                                std::vector<std::string>{"a", "b", "c", "d"}));
        pr_empty.apply(value);
        REQUIRE_THAT(value, Catch::Matchers::Equals(
                                std::vector<std::string>{"a", "b", "c", "d"}));
        ap_empty.apply(value);
        REQUIRE_THAT(value, Catch::Matchers::Equals(
                                std::vector<std::string>{"a", "b", "c", "d"}));
        ap_2.apply(value);
        REQUIRE_THAT(value, Catch::Matchers::Equals(std::vector<std::string>{
                                "a", "b", "c", "d", "e", "f"}));
    }
}

TEST_CASE("prefix_path simplify", "[envvars]") {
    REQUIRE_THAT(uenv::simplify_prefix_path_list({}),
                 Catch::Matchers::Equals(std::vector<std::string>{}));

    REQUIRE_THAT(uenv::simplify_prefix_path_list({"a"}),
                 Catch::Matchers::Equals(std::vector<std::string>{"a"}));

    REQUIRE_THAT(uenv::simplify_prefix_path_list({"a", "a"}),
                 Catch::Matchers::Equals(std::vector<std::string>{"a"}));

    REQUIRE_THAT(uenv::simplify_prefix_path_list({"a", "b", "a"}),
                 Catch::Matchers::Equals(std::vector<std::string>{"a", "b"}));

    REQUIRE_THAT(uenv::simplify_prefix_path_list({"c", "d"}),
                 Catch::Matchers::Equals(std::vector<std::string>{"c", "d"}));

    REQUIRE_THAT(uenv::simplify_prefix_path_list(
                     {"z", "hello", "apple", "cat", "apple", "z", "wombat"}),
                 Catch::Matchers::Equals(std::vector<std::string>{
                     "z", "hello", "apple", "cat", "wombat"}));
    REQUIRE_THAT(uenv::simplify_prefix_path_list(
                     {"/opt/cray/libfabric/1.15.2.0", "/usr/lib64",
                      "/opt/cray/libfabric/1.15.2.0"}),
                 Catch::Matchers::Equals(std::vector<std::string>{
                     "/opt/cray/libfabric/1.15.2.0", "/usr/lib64"}));
}

// check that conflicts are correctly flagged
TEST_CASE("envvarset validate", "[envvars]") {
    uenv::envvarset ev;
    REQUIRE_FALSE(ev.update_scalar("FOO", "wombat"));
    REQUIRE(ev.update_prefix_path("FOO", {uenv::update_kind::set, {"wombat"}}));
    REQUIRE_FALSE(ev.update_prefix_path("apple", {{}, {}}));
    REQUIRE(ev.update_scalar("apple", ""));
}

// check that final envvarset variables are correctly generated
TEST_CASE("envvarset get final values", "[envvars]") {
    uenv::envvarset ev;
    ev.update_scalar("ANIMAL", "wombat");
    ev.update_scalar("VEHICLE", "car");
    ev.update_scalar("ABODE", "tent");

    ev.update_prefix_path("A", {uenv::update_kind::set, {"c", "d"}});
    ev.update_prefix_path("B", {uenv::update_kind::prepend, {"a", "b"}});
    ev.update_prefix_path("C", {uenv::update_kind::append, {"c", "d"}});

    {
        auto getenv = []([[maybe_unused]] const std::string& name)
            -> std::optional<std::string> { return std::nullopt; };
        REQUIRE_THAT(ev.get_values(getenv),
                     Catch::Matchers::UnorderedEquals(
                         std::vector<uenv::scalar>{{"ABODE", "tent"},
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
                         std::vector<uenv::scalar>{{"ABODE", "tent"},
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
namespace environment {
bool validate_name(std::string_view name, bool strict = true);
}

TEST_CASE("validate envvar names", "[environment]") {
    REQUIRE(environment::validate_name("wombat", true));
    REQUIRE(environment::validate_name("_", true));
    REQUIRE(environment::validate_name("__", true));
    REQUIRE(environment::validate_name("_WOMBAT", true));
    REQUIRE(environment::validate_name("a", true));
    REQUIRE(environment::validate_name("A", true));
    REQUIRE(environment::validate_name("ab", true));
    REQUIRE(environment::validate_name("AB", true));
    REQUIRE(environment::validate_name("PATH", true));
    REQUIRE(environment::validate_name("CUDA_HOME", true));
    REQUIRE(environment::validate_name("P1", true));
    REQUIRE(environment::validate_name("_1", true));
    REQUIRE(environment::validate_name("a123_4", true));

    REQUIRE(!environment::validate_name("a-b", true));
    REQUIRE(!environment::validate_name("b?", true));
    REQUIRE(!environment::validate_name("-", true));
    REQUIRE(!environment::validate_name("!", true));
    REQUIRE(!environment::validate_name("wombat soup", true));
}

TEST_CASE("variables::clear", "[environment]") {
    {
        environment::variables E{};
        auto initial_vars = E.c_env();
        // assume that the calling environment has at least one environment
        // variabls
        REQUIRE(initial_vars != nullptr);
        REQUIRE(initial_vars[0] == nullptr);
        E.clear();
        auto final_vars = E.c_env();
        REQUIRE(final_vars != nullptr);
        REQUIRE(final_vars[0] == nullptr);
    }
    {
        environment::variables E{environ};
        auto initial_vars = E.c_env();
        // assume that the calling environment has at least one environment
        // variabls
        REQUIRE(initial_vars != nullptr);
        REQUIRE(initial_vars[0] != nullptr);
        E.clear();
        auto final_vars = E.c_env();
        REQUIRE(final_vars != nullptr);
        REQUIRE(final_vars[0] == nullptr);
    }
}

TEST_CASE("variables::set-get-unset", "[environment]") {
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

    environment::variables E{};

    {
        auto vars = E.c_env();
        REQUIRE(n_env(vars) == 0u);
    }

    E.set("hello", "world");
    REQUIRE(E.get("hello"));
    REQUIRE(E.get("hello").value() == "world");
    {

        auto vars = E.c_env();
        REQUIRE(n_env(vars) == 1u);
        REQUIRE(vars[0] == std::string("hello=world"));
    }
    E.set("hello", "there");
    REQUIRE(E.get("hello"));
    REQUIRE(E.get("hello").value() == "there");
    {

        auto vars = E.c_env();
        REQUIRE(n_env(vars) == 1u);
        REQUIRE(vars[0] == std::string("hello=there"));
    }
    E.set("another", "variable");
    REQUIRE(E.get("another"));
    REQUIRE(E.get("another").value() == "variable");
    REQUIRE(n_env(E.c_env()) == 2u);
    E.set("and", "another");
    REQUIRE(n_env(E.c_env()) == 3u);
    E.set("and", "overwrite");
    REQUIRE(n_env(E.c_env()) == 3u);
    // an illegal environment variable name will be ignored (a warning log
    // message is generated)
    E.set("and it was always thus", "antechinus");
    REQUIRE(n_env(E.c_env()) == 3u);
    REQUIRE(!E.get("and it was always thus"));

    REQUIRE(!E.get("A_VALID_NAME"));
    REQUIRE(!E.get("_"));

    // set a variable, then check that unset removes the variable
    E.set("wombat", "soup");
    REQUIRE(E.get("wombat").value() == "soup");
    E.unset("wombat");
    REQUIRE(!E.get("wombat"));
}
