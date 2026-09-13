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

// UENV_TELEMETRY is read from the environment of a slurm job or `uenv status`,
// so it is user input: anything that is not the exact shape written by
// to_string must be an error rather than an assertion failure.
TEST_CASE("parse_telemetry malformed", "[telemetry]") {
    for (
        const auto& body : {
            "",
            "not json",
            "{}",
            "42",
            "[1]",
            "[null]",
            "[[]]",
            "[{}]",
            R"([{"sqfs": "/y", "digest": null, "label": null, "name": "n", "views": []}])",
            R"([{"mount": "/x", "digest": null, "label": null, "name": "n", "views": []}])",
            R"([{"mount": "/x", "sqfs": "/y", "label": null, "name": "n", "views": []}])",
            R"([{"mount": "/x", "sqfs": "/y", "digest": null, "name": "n", "views": []}])",
            R"([{"mount": "/x", "sqfs": "/y", "digest": null, "label": null, "views": []}])",
            R"([{"mount": "/x", "sqfs": "/y", "digest": null, "label": null, "name": "n"}])",
            R"([{"mount": 1, "sqfs": "/y", "digest": null, "label": null, "name": "n", "views": []}])",
            R"([{"mount": "/x", "sqfs": "/y", "digest": 1, "label": null, "name": "n", "views": []}])",
            R"([{"mount": "/x", "sqfs": "/y", "digest": "zz", "label": null, "name": "n", "views": []}])",
            R"([{"mount": "/x", "sqfs": "/y", "digest": null, "label": "not a label", "name": "n", "views": []}])",
            R"([{"mount": "/x", "sqfs": "/y", "digest": null, "label": null, "name": "", "views": []}])",
            R"([{"mount": "/x", "sqfs": "/y", "digest": null, "label": null, "name": "n", "views": [1]}])",
            R"([{"mount": "/x", "sqfs": "/y", "digest": null, "label": null, "name": "n", "views": [""]}])",
            R"([{"mount": "relative", "sqfs": "/y", "digest": null, "label": null, "name": "n", "views": []}])",
        }) {
        INFO(body);
        auto out = uenv::parse_telemetry(body);
        REQUIRE(!out);
        REQUIRE(!out.error().empty());
    }

    // an empty array is a valid, empty, result
    auto out = uenv::parse_telemetry("[]");
    REQUIRE(out);
    REQUIRE(out->empty());
}
