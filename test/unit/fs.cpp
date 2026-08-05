#include <unistd.h>

#include <filesystem>

#include <catch2/catch_all.hpp>
#include <fmt/core.h>

#include <util/fs.h>
#include <util/subprocess.h>

namespace fs = std::filesystem;

TEST_CASE("make_temp_dir", "[fs]") {
    auto r1 = util::make_temp_dir();
    REQUIRE(r1);
    auto dir1 = *r1;
    REQUIRE(fs::is_directory(dir1));
    auto r2 = util::make_temp_dir();
    REQUIRE(r2);
    auto dir2 = *r2;
    REQUIRE(dir1 != dir2);

    REQUIRE(util::is_temp_dir(dir1));
    REQUIRE(util::is_temp_dir(dir1 / "meta"));
    REQUIRE(util::is_temp_dir(dir2));
    REQUIRE(util::is_temp_dir(dir2 / "wombat"));
    REQUIRE(!util::is_temp_dir(dir2 / ".."));
    REQUIRE(!util::is_temp_dir("/scratch/bar"));
}

TEST_CASE("ensure_directory", "[fs]") {
    auto base = util::make_temp_dir();
    REQUIRE(base);

    // creates a new (nested) directory
    {
        auto d = *base / "a" / "b" / "c";
        REQUIRE(util::ensure_directory(d));
        REQUIRE(fs::is_directory(d));
    }

    // succeeds when the directory already exists
    {
        auto d = *base / "exists";
        REQUIRE(util::ensure_directory(d));
        REQUIRE(util::ensure_directory(d));
    }

    // fails when the path exists but is a regular file
    {
        auto p = *base / "afile";
        std::ofstream(p).close();
        auto r = util::ensure_directory(p);
        REQUIRE(!r);
    }

    // fails when the directory is not writable (skip when running as root,
    // which bypasses permission checks)
    if (::geteuid() != 0) {
        auto d = *base / "readonly";
        REQUIRE(util::ensure_directory(d));
        fs::permissions(d, fs::perms::owner_read | fs::perms::owner_exec,
                        fs::perm_options::replace);
        auto r = util::ensure_directory(d);
        REQUIRE(!r);
        // restore so cleanup can remove it
        fs::permissions(d, fs::perms::owner_all, fs::perm_options::replace);
    }
}

TEST_CASE("unsquashfs", "[fs]") {
    auto exe = util::exe_path();

    if (!exe) {
        SKIP("unable to determine the path of the unit executable");
    }
    auto sqfs =
        exe->parent_path() / "data/sqfs/apptool/standalone/app43.squashfs";
    if (!fs::is_regular_file(sqfs)) {
        SKIP("unable to find the squashfs file for testing");
    }
    {
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

TEST_CASE("read_single_line_file", "[fs]") {
    // file does not exist
    REQUIRE(!util::read_single_line_file("/wombat/soup"));
    REQUIRE(util::read_single_line_file("/etc/hostname"));

    auto testdir = util::make_temp_dir().value();

    // empty file
    {
        auto p = testdir / "empty";
        std::ofstream(p).close();
        REQUIRE(!util::read_single_line_file(p));
    }

    // file with a single space
    {
        auto p = testdir / "onespace";
        (std::ofstream(p) << " ").close();
        auto r = util::read_single_line_file(p);
        REQUIRE(r);
        REQUIRE(r.value() == " ");
    }

    // file with an empty line
    {
        auto p = testdir / "nilline";
        (std::ofstream(p) << "\n").close();
        auto r = util::read_single_line_file(p);
        REQUIRE(r);
        REQUIRE(r.value() == "");
    }

    // file with a single line and no new line
    {
        auto p = testdir / "oneline";
        (std::ofstream(p) << "uenv v9.1.0-dev").close();
        auto r = util::read_single_line_file(p);
        REQUIRE(r);
        REQUIRE(r.value() == "uenv v9.1.0-dev");
    }

    // file with a single line followed by new line
    {
        auto p = testdir / "onenewline";
        (std::ofstream(p) << "uenv v9.1.0-dev\n").close();
        auto r = util::read_single_line_file(p);
        REQUIRE(r);
        REQUIRE(r.value() == "uenv v9.1.0-dev");
    }

    // file with two non-trivial lines
    {
        auto p = testdir / "twoline";
        (std::ofstream(p) << "hello world\nhoi stranger").close();
        auto r = util::read_single_line_file(p);
        REQUIRE(r);
        REQUIRE(r.value() == "hello world");
    }
}

TEST_CASE("is_child", "[fs]") {
    // direct child
    {
        const std::filesystem::path child = "/path/to/child";
        const std::filesystem::path parent = "/path/to";
        REQUIRE(util::is_child(child, parent));
    }

    // indirect child
    {
        const std::filesystem::path child = "/path/to/child";
        const std::filesystem::path parent = "/path";
        REQUIRE(util::is_child(child, parent));
    }

    // not a child
    {
        const std::filesystem::path child = "/path/to/child";
        const std::filesystem::path parent = "/tmp";
        REQUIRE(!util::is_child(child, parent));
    }
}
