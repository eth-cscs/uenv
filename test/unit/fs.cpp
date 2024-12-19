#include <filesystem>

#include <catch2/catch_all.hpp>
#include <fmt/core.h>

#include <util/fs.h>
#include <util/subprocess.h>

namespace fs = std::filesystem;

TEST_CASE("make_temp_dir", "[fs]") {
    auto dir1 = util::make_temp_dir();
    REQUIRE(fs::is_directory(dir1));
    auto dir2 = util::make_temp_dir();
    REQUIRE(dir1 != dir2);
}

TEST_CASE("unsquashfs", "[fs]") {
    std::string sqfs = "./test/data/sqfs/apptool/standalone/app43.squashfs";
    {
        REQUIRE(fs::is_regular_file(sqfs));
        auto meta = util::unsquashfs_tmp(sqfs, "meta");
        REQUIRE(meta);
        REQUIRE(fs::is_directory(*meta));
        REQUIRE(fs::is_directory(*meta / "meta"));
        REQUIRE(fs::is_regular_file(*meta / "meta" / "env.json"));
    }
    // unpack from a squashfs image many times to validate that the unpacked
    // data is persistant.
    {
        const int nbuf = 128;
        {
            std::vector<fs::path> paths;
            for (int i = 0; i < nbuf; ++i) {
                auto meta = util::unsquashfs_tmp(sqfs, "meta/env.json");
                REQUIRE(meta);
                auto file = *meta / "meta/env.json";
                REQUIRE(fs::is_regular_file(file));
                paths.push_back(file);
            }

            // the generated paths should be unique
            std::sort(paths.begin(), paths.end());
            auto e = std::unique(paths.begin(), paths.end());
            REQUIRE(e == paths.end());

            // the generated paths should still exist
            for (auto& file : paths) {
                REQUIRE(fs::is_regular_file(file));
            }
        }
    }
}
