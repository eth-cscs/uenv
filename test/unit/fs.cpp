#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <catch2/catch_all.hpp>
#include <fmt/core.h>

#include <util/defer.h>
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

    // a path is inside itself
    REQUIRE(util::is_child("/data", "/data"));
    // sibling with a shared string prefix
    REQUIRE(!util::is_child("/data2", "/data"));
    // the parent is not inside the child
    REQUIRE(!util::is_child("/data", "/data/foo"));
}

TEST_CASE("read_regular_file", "[fs]") {
    const auto dir = *util::make_temp_dir();

    std::ofstream(dir / "file") << "hello";
    REQUIRE(util::read_regular_file(dir / "file", 100) == "hello");
    REQUIRE(util::read_regular_file(dir / "file", 5) == "hello");
    // too large
    REQUIRE(util::read_regular_file(dir / "file", 4) == std::nullopt);
    // missing, a directory, and a FIFO, which must not block
    REQUIRE(util::read_regular_file(dir / "missing", 100) == std::nullopt);
    REQUIRE(util::read_regular_file(dir, 100) == std::nullopt);
    REQUIRE(::mkfifo((dir / "fifo").c_str(), 0600) == 0);
    REQUIRE(util::read_regular_file(dir / "fifo", 100) == std::nullopt);

    fs::remove_all(dir);
}

TEST_CASE("write_file_atomic", "[fs]") {
    const auto dir = *util::make_temp_dir();
    auto entries = [&dir] {
        return std::distance(fs::directory_iterator(dir),
                             fs::directory_iterator{});
    };

    REQUIRE(util::write_file_atomic(dir / "file", "one"));
    REQUIRE(util::read_file(dir / "file") == "one");
    REQUIRE(util::write_file_atomic(dir / "file", "two"));
    REQUIRE(util::read_file(dir / "file") == "two");
    // no temporary file is left
    REQUIRE(entries() == 1);

    // a directory in the way is an error, and no temporary file is left
    fs::create_directory(dir / "dir");
    REQUIRE(!util::write_file_atomic(dir / "dir", "three"));
    REQUIRE(fs::is_directory(dir / "dir"));
    REQUIRE(entries() == 2);

    REQUIRE(!util::write_file_atomic(dir / "missing" / "file", "four"));

    fs::remove_all(dir);
}

TEST_CASE("touch and modified_within", "[fs]") {
    using namespace std::chrono_literals;
    const auto dir = *util::make_temp_dir();
    const auto file = dir / "stamp";

    REQUIRE(!util::modified_within(file, 1h));
    REQUIRE(util::touch(file));
    REQUIRE(util::modified_within(file, 1h));

    // old, and from a clock that was ahead
    fs::last_write_time(file, fs::file_time_type::clock::now() - 2h);
    REQUIRE(!util::modified_within(file, 1h));
    fs::last_write_time(file, fs::file_time_type::clock::now() + 2h);
    REQUIRE(!util::modified_within(file, 1h));

    // touch resets the time
    REQUIRE(util::touch(file));
    REQUIRE(util::modified_within(file, 1h));

    // not a directory, and not through a symbolic link
    REQUIRE(!util::touch(dir));
    fs::create_symlink(dir / "target", dir / "link");
    REQUIRE(!util::touch(dir / "link"));
    REQUIRE(!fs::exists(dir / "target"));

    fs::remove_all(dir);
}

TEST_CASE("path_exists, path_is_file and path_is_dir", "[fs]") {
    const auto root = util::make_temp_dir().value();
    const auto file = root / "file";
    std::ofstream{file} << "x";
    const auto dir = root / "dir";
    fs::create_directory(dir);
    const auto link_file = root / "link-file";
    fs::create_symlink(file, link_file);
    const auto link_dir = root / "link-dir";
    fs::create_symlink(dir, link_dir);
    const auto dangling = root / "dangling";
    fs::create_symlink(root / "missing", dangling);

    struct expect {
        fs::path path;
        bool exists;
        bool is_file;
        bool is_dir;
    };
    const std::vector<expect> cases{
        {file, true, true, false},
        {dir, true, false, true},
        // symlinks are followed
        {link_file, true, true, false},
        {link_dir, true, false, true},
        {dangling, false, false, false},
        {root / "missing", false, false, false},
        // ENOTDIR: a path through a regular file
        {file / "x", false, false, false},
        // ENAMETOOLONG: the throwing std::filesystem overloads throw here
        {root / std::string(300, 'a'), false, false, false},
        {fs::path("/") / std::string(5000, 'a'), false, false, false},
    };
    for (const auto& c : cases) {
        INFO(c.path.string().substr(0, 100));
        bool exists{}, is_file{}, is_dir{};
        REQUIRE_NOTHROW(exists = util::path_exists(c.path));
        REQUIRE_NOTHROW(is_file = util::path_is_file(c.path));
        REQUIRE_NOTHROW(is_dir = util::path_is_dir(c.path));
        REQUIRE(exists == c.exists);
        REQUIRE(is_file == c.is_file);
        REQUIRE(is_dir == c.is_dir);
    }

    // EACCES: a file in a directory that can't be searched
    if (geteuid() != 0) {
        const auto locked = root / "locked";
        fs::create_directory(locked);
        std::ofstream{locked / "file"} << "x";
        fs::permissions(locked, fs::perms::none);
        auto restore = util::defer(
            [&locked]() { fs::permissions(locked, fs::perms::owner_all); });

        bool exists{}, is_file{};
        REQUIRE_NOTHROW(exists = util::path_exists(locked / "file"));
        REQUIRE_NOTHROW(is_file = util::path_is_file(locked / "file"));
        REQUIRE(!exists);
        REQUIRE(!is_file);
    }
}

TEST_CASE("absolute_path", "[fs]") {
    const auto cwd = fs::current_path();

    {
        auto p = util::absolute_path("/wombat/soup");
        REQUIRE(p);
        REQUIRE(*p == "/wombat/soup");
    }
    {
        auto p = util::absolute_path("wombat");
        REQUIRE(p);
        REQUIRE(*p == cwd / "wombat");
    }

    // a relative path can't be made absolute when the current directory has
    // been deleted: getcwd fails with ENOENT
    {
        const auto gone = util::make_temp_dir().value();
        fs::current_path(gone);
        auto restore = util::defer([&cwd]() { fs::current_path(cwd); });
        fs::remove(gone);

        util::expected<fs::path, std::string> p = fs::path{};
        REQUIRE_NOTHROW(p = util::absolute_path("wombat"));
        REQUIRE(!p);
        // an absolute path does not need the current directory
        REQUIRE(util::absolute_path("/wombat"));
    }
}

TEST_CASE("file_creation_date", "[fs]") {
    const auto root = util::make_temp_dir().value();
    const auto file = root / "file";
    std::ofstream{file} << "x";

    REQUIRE(util::file_creation_date(file));

    auto missing = util::file_creation_date(root / "missing");
    REQUIRE(!missing);
    REQUIRE_THAT(missing.error(), Catch::Matchers::ContainsSubstring(
                                      "No such file or directory"));
}

TEST_CASE("unsquashfs_tmp invalid input", "[fs]") {
    const auto root = util::make_temp_dir().value();

    // the input is checked before unsquashfs is run
    auto missing = util::unsquashfs_tmp(root / "missing.squashfs", "meta");
    REQUIRE(!missing);
    REQUIRE_THAT(missing.error(), Catch::Matchers::ContainsSubstring(
                                      "No such file or directory"));

    auto dir = util::unsquashfs_tmp(root, "meta");
    REQUIRE(!dir);
    REQUIRE_THAT(dir.error(),
                 Catch::Matchers::ContainsSubstring("not a regular file"));

    auto too_long = util::unsquashfs_tmp(root / std::string(300, 'a'), "meta");
    REQUIRE(!too_long);
    REQUIRE_THAT(too_long.error(),
                 Catch::Matchers::ContainsSubstring("File name too long"));
}

TEST_CASE("path_type, check_file and check_dir", "[fs]") {
    using Catch::Matchers::ContainsSubstring;
    using fs::file_type;

    const auto root = util::make_temp_dir().value();
    const auto file = root / "file";
    std::ofstream{file} << "x";
    const auto dir = root / "dir";
    fs::create_directory(dir);
    const auto too_long = root / std::string(300, 'a');

    // a path that does not exist is not an error
    REQUIRE(util::path_type(root / "missing") == file_type::not_found);
    REQUIRE(util::path_type(file / "x") == file_type::not_found); // ENOTDIR
    REQUIRE(util::path_type(file) == file_type::regular);
    REQUIRE(util::path_type(dir) == file_type::directory);
    // a path that can't be examined is
    {
        auto t = util::path_type(too_long);
        REQUIRE(!t);
        REQUIRE(t.error() == std::errc::filename_too_long);
    }

    REQUIRE(util::check_file(file));
    REQUIRE(util::check_dir(dir));
    REQUIRE(util::check_file(root / "missing").error() == "does not exist");
    REQUIRE(util::check_dir(root / "missing").error() == "does not exist");
    REQUIRE(util::check_file(dir).error() == "is not a regular file");
    REQUIRE(util::check_dir(file).error() == "is not a directory");
    REQUIRE_THAT(util::check_dir(too_long).error(),
                 ContainsSubstring("can't be accessed: File name too long"));

    if (geteuid() != 0) {
        const auto locked = root / "locked";
        fs::create_directory(locked);
        std::ofstream{locked / "file"} << "x";
        fs::permissions(locked, fs::perms::none);
        auto restore = util::defer(
            [&locked]() { fs::permissions(locked, fs::perms::owner_all); });

        auto t = util::path_type(locked / "file");
        REQUIRE(!t);
        REQUIRE(t.error() == std::errc::permission_denied);
        REQUIRE_THAT(util::check_file(locked / "file").error(),
                     ContainsSubstring("can't be accessed: Permission denied"));
    }
}
