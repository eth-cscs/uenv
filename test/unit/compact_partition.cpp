#include <catch2/catch_all.hpp>

#include <fmt/format.h>

#include <util/compact_partition.h>

TEST_CASE("print", "[compact_partition]") {
    {
        util::compact_partition part;
        part.add_section(1, 8);
        REQUIRE(fmt::format("{}", part) == "8");
    }
    {
        util::compact_partition part;
        part.add_section(3, 8);
        REQUIRE(fmt::format("{}", part) == "8(x3)");
    }
    {
        util::compact_partition part;
        part.add_section(1, 3);
        part.add_section(4, 2);
        REQUIRE(fmt::format("{}", part) == "3,2(x4)");
    }
}

TEST_CASE("find_slot", "[compact_partition]") {
    // "5,4(x3)": slot 0 holds ranks [0,5), slot 1 holds [5,9), slot 2 holds
    // [9,13), slot 3 holds [13,17)
    {
        util::compact_partition part;
        part.add_section(1, 5);
        part.add_section(3, 4);

        REQUIRE(part.find_slot(0) == 0u);
        REQUIRE(part.find_slot(4) == 0u);
        REQUIRE(part.find_slot(5) == 1u);
        REQUIRE(part.find_slot(8) == 1u);
        REQUIRE(part.find_slot(9) == 2u);
        REQUIRE(part.find_slot(12) == 2u);
        REQUIRE(part.find_slot(13) == 3u);
        REQUIRE(part.find_slot(16) == 3u);

        // out of bounds: total size is 5+4*3=17, so rank 17 and beyond
        REQUIRE_FALSE(part.find_slot(17));
        REQUIRE_FALSE(part.find_slot(100));
    }
    // uniform partition "4(x4)": slot = rank / 4
    {
        util::compact_partition part;
        part.add_section(4, 4);

        REQUIRE(part.find_slot(0) == 0u);
        REQUIRE(part.find_slot(3) == 0u);
        REQUIRE(part.find_slot(4) == 1u);
        REQUIRE(part.find_slot(7) == 1u);
        REQUIRE(part.find_slot(15) == 3u);
        REQUIRE_FALSE(part.find_slot(16));
    }
    // single-slot partition "8"
    {
        util::compact_partition part;
        part.add_section(1, 8);

        REQUIRE(part.find_slot(0) == 0u);
        REQUIRE(part.find_slot(7) == 0u);
        REQUIRE_FALSE(part.find_slot(8));
    }
}

TEST_CASE("find_size", "[compact_partition]") {
    // "5,4(x3)": slot 0 has size 5, slots 1-3 have size 4
    {
        util::compact_partition part;
        part.add_section(1, 5);
        part.add_section(3, 4);

        REQUIRE(part.find_size(0) == 5u);
        REQUIRE(part.find_size(1) == 4u);
        REQUIRE(part.find_size(2) == 4u);
        REQUIRE(part.find_size(3) == 4u);

        // out of bounds: 4 slots total (1+3), so slot 4 and beyond
        REQUIRE_FALSE(part.find_size(4));
        REQUIRE_FALSE(part.find_size(100));
    }
    // uniform partition "8(x3)"
    {
        util::compact_partition part;
        part.add_section(3, 8);

        REQUIRE(part.find_size(0) == 8u);
        REQUIRE(part.find_size(1) == 8u);
        REQUIRE(part.find_size(2) == 8u);
        REQUIRE_FALSE(part.find_size(3));
    }
    // single-slot partition "8"
    {
        util::compact_partition part;
        part.add_section(1, 8);

        REQUIRE(part.find_size(0) == 8u);
        REQUIRE_FALSE(part.find_size(1));
    }
}

TEST_CASE("parse", "[compact_partition]") {
    {
        auto part = util::parse_compact_partition("8");
        REQUIRE(part);
        REQUIRE(part->sections().size() == 1);
        REQUIRE(part->sections()[0].count == 1);
        REQUIRE(part->sections()[0].size == 8);
        REQUIRE(fmt::format("{}", *part) == "8");
    }
    {
        auto part = util::parse_compact_partition("8(x3)");
        REQUIRE(part);
        REQUIRE(part->sections().size() == 1);
        REQUIRE(part->sections()[0].count == 3);
        REQUIRE(part->sections()[0].size == 8);
        REQUIRE(fmt::format("{}", *part) == "8(x3)");
    }
    {
        auto part = util::parse_compact_partition("3,2(x4)");
        REQUIRE(part);
        REQUIRE(part->sections().size() == 2);
        REQUIRE(part->sections()[0].count == 1);
        REQUIRE(part->sections()[0].size == 3);
        REQUIRE(part->sections()[1].count == 4);
        REQUIRE(part->sections()[1].size == 2);
        REQUIRE(fmt::format("{}", *part) == "3,2(x4)");
    }
    {
        // SLURM_STEP_TASKS_PER_NODE-style example, from the docs
        auto part = util::parse_compact_partition("5,4(x3)");
        REQUIRE(part);
        REQUIRE(part->sections().size() == 2);
        REQUIRE(part->sections()[0].count == 1);
        REQUIRE(part->sections()[0].size == 5);
        REQUIRE(part->sections()[1].count == 3);
        REQUIRE(part->sections()[1].size == 4);
    }
    {
        // whitespace is tolerated around the value
        auto part = util::parse_compact_partition("  5,4(x3)  ");
        REQUIRE(part);
        REQUIRE(fmt::format("{}", *part) == "5,4(x3)");
    }
    {
        // a trailing comma is tolerated
        auto part = util::parse_compact_partition("4,2,");
        REQUIRE(part);
        REQUIRE(fmt::format("{}", *part) == "4,2");
    }
}

TEST_CASE("parse errors", "[compact_partition]") {
    REQUIRE_FALSE(util::parse_compact_partition(""));
    REQUIRE_FALSE(util::parse_compact_partition("a"));
    REQUIRE_FALSE(util::parse_compact_partition("4(3)"));   // missing 'x'
    REQUIRE_FALSE(util::parse_compact_partition("4(x3"));   // missing ')'
    REQUIRE_FALSE(util::parse_compact_partition("4(x)"));   // missing COUNT
    REQUIRE_FALSE(util::parse_compact_partition(","));
    REQUIRE_FALSE(util::parse_compact_partition("4 garbage")); // trailing input
}
