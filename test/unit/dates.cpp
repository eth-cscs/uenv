#include <catch2/catch_all.hpp>

#include <uenv/uenv.h>

using dt = uenv::uenv_date;
TEST_CASE("equality", "[dates]") {
    REQUIRE(dt{2024, 02, 01} == dt{2024, 02, 01});
    REQUIRE(dt{2024, 02, 01} != dt{2025, 02, 01});
    REQUIRE(dt{2024, 02, 01} != dt{2024, 03, 01});
    REQUIRE(dt{2024, 02, 01} != dt{2024, 02, 02});
}

TEST_CASE("range", "[dates]") {
    REQUIRE(dt(2024, 2, 1).validate());
    REQUIRE(dt(2024, 1, 31).validate());
    REQUIRE(dt(2024, 12, 1).validate());
    REQUIRE(dt(2024, 2, 29).validate());
    REQUIRE(dt(2023, 2, 28).validate());
    REQUIRE(!dt(2023, 2, 29).validate());
    REQUIRE(!dt(2023, 0, 29).validate());
    REQUIRE(!dt(2023, 1, 32).validate());
    REQUIRE(!dt(2023, 13, 1).validate());
}

TEST_CASE("comparison", "[dates]") {
    REQUIRE(dt{2024, 2, 1} < dt{2025, 2, 1});
    REQUIRE(dt{2024, 2, 1} < dt{2024, 3, 1});
    REQUIRE(dt{2024, 2, 1} < dt{2024, 2, 2});
}

TEST_CASE("printing", "[dates]") {
    REQUIRE(fmt::format("{}", dt{2024, 2, 1}) == "2024-02-01 00:00:00");
    REQUIRE(fmt::format("{:l}", dt{2024, 2, 1}) == "2024-02-01 00:00:00");
    REQUIRE(fmt::format("{:s}", dt{2024, 2, 1}) == "2024-02-01");
    REQUIRE(fmt::format("{}", dt{2024, 12, 29}) == "2024-12-29 00:00:00");
    REQUIRE(fmt::format("{}", dt{2024, 12, 29, 12, 17, 3}) ==
            "2024-12-29 12:17:03");
    REQUIRE(fmt::format("{:s}", dt{2024, 12, 29, 12, 17, 3}) == "2024-12-29");
}
