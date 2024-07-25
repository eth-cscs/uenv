#include <catch2/catch_all.hpp>

#include <vector>

#include <uenv/envvars.h>

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
        auto getenv =
            []([[maybe_unused]] const std::string& name) -> const char* {
            return nullptr;
        };
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
        auto getenv = [](const std::string& name) -> const char* {
            if (name == "A")
                return "hello";
            if (name == "B")
                return "c:d";
            if (name == "C")
                return "a:b:b:a:c:a";
            return nullptr;
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

// std::vector<std::string>
// simplify_prefix_path_list(const std::vector<std::string> &in);
