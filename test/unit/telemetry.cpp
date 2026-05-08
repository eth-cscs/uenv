#include <catch2/catch_all.hpp>
#include <fmt/format.h>

#include <uenv/telemetry.h>

TEST_CASE("tostring", "[telemetry]") {
    std::vector<uenv::telemetry_data> in{
        {
            .mount = "/uenv",
            .sqfs = "/images/store.squashfs",
            .digest = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                      "aaaaaaaa",
            .views = {"a", "b"},
            .label = "tools/25.6:v2@eiger%gh200",
            .name = "tools",
        },
        {
            .mount = "/user-environment",
            .sqfs = "/uenv/team/images/store.squashfs",
            .digest = std::nullopt,
            .views = {},
            .label = std::nullopt,
            .name = "tools",
        },
    };

    // do a round trip to string and back again
    auto out = uenv::parse_telemetry(to_string(in));

    REQUIRE(out);

    for (unsigned i = 0u; i < in.size(); ++i) {
        const auto& lhs = out.value()[i];
        const auto& rhs = in[i];
        REQUIRE(lhs.mount == rhs.mount);
        REQUIRE(lhs.sqfs == rhs.sqfs);
        REQUIRE(lhs.digest == rhs.digest);
        REQUIRE(lhs.views == rhs.views);
        REQUIRE(lhs.label == rhs.label);
        REQUIRE(lhs.name == rhs.name);
    }
}
