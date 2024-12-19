#include <catch2/catch_all.hpp>
#include <fmt/core.h>

#include <uenv/env.h>
#include <uenv/log.h>
#include <uenv/meta.h>
#include <util/fs.h>

TEST_CASE("load_meta", "[env]") {
    auto exe = util::exe_path();
    if (!exe) {
        SKIP("unable to find path of unit executable");
    }
    auto meta_path = exe->parent_path() / "data/env-files/cp2k-2024.2-v1.json";
    REQUIRE(uenv::load_meta(meta_path));
}
