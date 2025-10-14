#include <charconv>
#include <filesystem>

#include <sys/vfs.h>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <util/envvars.h>
#include <util/expected.h>
#include <util/lustre.h>
#include <util/shell.h>
#include <util/subprocess.h>

namespace lustre {

bool is_lustre(const std::filesystem::path& p) {
    constexpr std::size_t luster_magic = 0x0BD00BD0;

    if (!exists(p)) {
        return false;
    }

    if (is_regular_file(p) || is_directory(p)) {
        struct statfs fsinfo;
        if (statfs(p.c_str(), &fsinfo) == -1) {
            return false;
        }
        return fsinfo.f_type == luster_magic;
    }

    // not a file or directory
    return false;
}

// Call lfs and parse the output to stdout.
// Output is expected to be a signed integer.
// used to facilitate calls to 'lfs getstripe', which return a single value to
// the terminal.
std::optional<std::int64_t> query_lfs(const std::vector<std::string> args) {
    auto result = util::run(args);

    if (!result) {
        spdlog::trace("lustre::lfs: error calling lfs ", result.error());
        return std::nullopt;
    }
    if (result->wait() != 0) {
        spdlog::trace("lustre::lfs: error waiting for lfs ", result->rvalue());
        return std::nullopt;
    }

    auto line = result->out.getline();
    if (!line) {
        return std::nullopt;
    }

    std::int64_t value;
    auto [ptr, ec] =
        std::from_chars(line->data(), line->data() + line->size(), value);

    if (ec == std::errc()) {
        spdlog::trace("lustre::lfs: {} -> {}", fmt::join(args, " "), value);
        return value;
    }

    spdlog::trace("lustre::lfs: {} -> error", fmt::join(args, " "));
    return std::nullopt;
}

util::expected<status, error> getstripe(const std::filesystem::path& p,
                                        const std::filesystem::path& lfs) {
    status s;

    // lfs getstripe --stripe-count $path
    if (const auto value = query_lfs(
            {lfs.string(), "getstripe", "--stripe-count", p.string()})) {
        s.count = *value;
    } else {
        return util::unexpected{error::lfs};
    }

    // lfs getstripe --stripe-size $path
    if (const auto value = query_lfs(
            {lfs.string(), "getstripe", "--stripe-size", p.string()})) {
        s.size = *value;
    } else {
        return util::unexpected{error::lfs};
    }

    // lfs getstripe --stripe-index $path
    if (const auto value = query_lfs(
            {lfs.string(), "getstripe", "--stripe-index", p.string()})) {
        s.index = *value;
    } else {
        return util::unexpected{error::lfs};
    }

    spdlog::debug("lustre::getstripe {} -> {}", p.string(), s);

    return s;
}

namespace impl {

// recursively apply lustre striping to a directory and its contents.
// iterates over each sub directory, and applies striping to every file and path
// in the directory
/*
bool setstripe(const std::filesystem::path& path, const status& config,
               const std::filesystem::path& lfs, bool recursive,
               bool migrate_files) {
    std::error_code ec;

    std::filesystem::directory_iterator dir_it{path, ec};
    if (ec) {
        return false;
    }

    // return after applying changes to the directory if no recursion is
    // requested
    if (!recursive) {
        return true;
    }

    for (const auto& p : dir_it) {
        if (p.is_directory(ec)) {
            if (!ec) {
                // lfs setstripe $path
                //      --size config.size
                //      --index config.index
                //      --count config.count
                std::vector<std::string> cmd = {
                    lfs.string(),
                    "setstripe",
                    p.path().string(),
                    fmt::format("--size={}", config.size),
                    fmt::format("--index={}", config.index),
                    fmt::format("--count={}", config.count)};
                if (auto result = util::run(cmd); !result || !result->wait()) {
                    spdlog::trace("error running '{}'", fmt::join(cmd, " "));
                    return false;
                }
                spdlog::trace("{}", fmt::join(cmd, " "));
                setstripe(p.path(), config, lfs, recursive, migrate_files);
            }
        } else if (p.is_regular_file(ec) && migrate_files) {
            if (!ec) {
                // lfs migrate $path
                //      --size config.size
                //      --index config.index
                //      --count config.count
                std::vector<std::string> cmd = {
                    lfs.string(),
                    "migrate",
                    p.path().string(),
                    fmt::format("--size={}", config.size),
                    fmt::format("--index={}", config.index),
                    fmt::format("--count={}", config.count)};
                if (auto result = util::run(cmd); !result || !result->wait()) {
                    spdlog::trace("error running '{}'", fmt::join(cmd, " "));
                    return false;
                }
                spdlog::trace("{}", fmt::join(cmd, " "));
            }
        }
    }
    return false;
}
*/
} // namespace impl

/*
bool setstripe(const std::filesystem::path& p, const status& config,
               const envvars::state& env, bool recursive, bool apply_to_files) {
    // check that the path is a directory on a lustre file system
    if (!is_lustre(p) || !std::filesystem::is_directory(p)) {
        spdlog::trace("lfs setstripe skipping: {} is not a lustre directory",
                      p.string());
        return false;
    }

    if (auto lfs = util::which("lfs", env.get("PATH").value_or(""))) {
        return impl::setstripe(p, config, lfs.value(), recursive,
                               apply_to_files);
    }
    spdlog::trace("lfs setstripe skipping: lfs not found");

    return false;
}
*/

util::expected<lpath, error> loadpath(const std::filesystem::path& p,
                                      const envvars::state& env) {
    if (!is_lustre(p)) {
        return util::unexpected{error::not_lustre};
    }

    // get lustre status of p
    auto lfs = util::which("lfs", env.get("PATH").value_or(""));
    if (!lfs) {
        return util::unexpected{error::no_lfs};
    }

    const auto config = getstripe(p, *lfs);
    if (!config) {
        return util::unexpected{config.error()};
    }

    return lpath{.config = *config, .path = p, .lfs = *lfs};
}

bool is_striped(const lpath& p) {
    return
        // files always have an explicit count >= 1
        (p.is_regular_file() && p.config.count > 1) ||
        // count==-1 on a directory implies "stripe contents over all OST"
        (p.is_directory() && (p.config.count < 0 || p.config.count > 1));
}

} // namespace lustre
