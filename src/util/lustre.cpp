#include <charconv>
#include <filesystem>

#include <sys/vfs.h>

#include <barkeep/barkeep.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <util/color.h>
#include <util/envvars.h>
#include <util/expected.h>
#include <util/lustre.h>
#include <util/shell.h>
#include <util/subprocess.h>

namespace lustre {

namespace impl {

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

util::expected<void, std::string>
call_lfs(const std::vector<std::string>& args) {
    spdlog::trace("{}", fmt::join(args, " "));

    auto result = util::run(args);

    if (!result) {
        return util::unexpected{result.error()};
    }
    if (auto r = result->wait(); r != 0) {
        auto line = result->err.getline();
        if (line) {
            return util::unexpected(*line);
        }
        return util::unexpected(fmt::format("lfs error code {}", r));
    }

    return {};
}

} // namespace impl

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
    // striping is only applied to regular files and directories
    return false;
}

#define QUERY_LFS(ARGS, X)                                                     \
    {                                                                          \
        if (const auto rval__ = lustre::impl::query_lfs ARGS)                  \
            X = *rval__;                                                       \
        else                                                                   \
            return util::unexpected(error::lfs);                               \
    }

util::expected<status, error> getstripe(const std::filesystem::path& p,
                                        const std::filesystem::path& lfs) {
    status s;

    QUERY_LFS(({lfs.string(), "getstripe", "--stripe-count", p.string()}),
              s.count);
    QUERY_LFS(({lfs.string(), "getstripe", "--stripe-size", p.string()}),
              s.size);
    QUERY_LFS(({lfs.string(), "getstripe", "--stripe-index", p.string()}),
              s.index);

    spdlog::debug("lustre::getstripe {} -> {}", p.string(), s);

    return s;
}

util::expected<lpath, error> load_path(const std::filesystem::path& p,
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

util::expected<void, std::string> apply(const lpath& path, lpath_apply f,
                                        bool recursive) {
    if (!recursive || path.is_regular_file()) {
        return f(path);
    }

    if (auto result = f(path); !result) {
        return result;
    }

    std::error_code ec;

    auto dir_it = std::filesystem::directory_iterator{path.path, ec};
    if (ec) {
        return util::unexpected{fmt::format("file system error {}:{}",
                                            path.path.string(), ec.message())};
    }

    for (const auto& it : dir_it) {
        const auto p = it.path();
        // Only apply to directories not named meta and regular files
        // This will stop recursion into meta paths, that do not contain
        // contents that require striping.
        if ((is_directory(p) && p.stem().string() != "meta") ||
            is_regular_file(p)) {
            const auto config = getstripe(p, path.lfs);
            if (!config) {
                return util::unexpected{fmt::format("{}", config.error())};
            }
            if (auto result = apply(
                    {.config = *config, .path = p, .lfs = path.lfs}, f, true);
                !result) {
                return result;
            }
        }
    }

    return {};
}

util::expected<void, std::string> print(const lpath& path) {
    if (path.is_regular_file() && path.path.filename() == "store.squashfs") {
        fmt::println("{} {}", color::blue(path.path.string()), path.config);
    } else if (path.is_directory()) {
        fmt::println("{} {}", color::yellow(path.path.string()), path.config);
    }
    return {};
}

void set_striping(const lpath& path, const status& config, bool verbose) {
    namespace bk = barkeep;
    using namespace std::chrono_literals;

    auto stats = is_striped(path);

    unsigned total_files = 0;
    unsigned total_dirs = 0;

    auto bars =
        bk::Composite({bk::ProgressBar(&total_files,
                                       {
                                           .total = stats.files.no,
                                           .message = "Files      ",
                                           .speed = 0,
                                           .speed_unit = "files/s",
                                           .style = bk::Rich,
                                           .no_tty = !isatty(fileno(stdout)),
                                           .show = false,
                                       }),
                       bk::ProgressBar(&total_dirs,
                                       {
                                           .total = stats.directories.no,
                                           .message = "Directories",
                                           .speed = 0,
                                           .speed_unit = "directories/s",
                                           .style = bk::Rich,
                                           .no_tty = !isatty(fileno(stdout)),
                                           .show = false,
                                       })},
                      "\n");
    if (verbose) {
        bars->show();
    }

    apply(
        path,
        [&](const lpath& p) -> util::expected<void, std::string> {
            if (p.is_regular_file() && p.path.filename() == "store.squashfs") {
                if (p.config.count < 2) {
                    spdlog::debug("lustre::set_striping {}", p.path.string());
                    // lfs migrate path --size={} --count={}
                    std::vector<std::string> cmd = {
                        p.lfs.string(), "migrate", p.path.string(),
                        fmt::format("--stripe-size={}", config.size),
                        fmt::format("--stripe-count={}", config.count)};
                    if (auto result = impl::call_lfs(cmd); !result) {
                        spdlog::error(result.error());
                    }
                    ++total_files;
                }
            } else if (p.is_directory()) {
                if (p.config.count == 1) {
                    spdlog::debug("lustre::set_striping {}", p.path.string());
                    // lfs setstripe path --size={} --count={}
                    std::vector<std::string> cmd = {
                        p.lfs.string(), "setstripe", p.path.string(),
                        fmt::format("--stripe-size={}", config.size),
                        fmt::format("--stripe-count={}", config.count)};
                    spdlog::trace("{}", fmt::join(cmd, " "));
                    if (auto result = impl::call_lfs(cmd); !result) {
                        spdlog::error(result.error());
                    }
                    ++total_dirs;
                }
            }
            return {};
        },
        true);

    bars->done();
}

stripe_stats is_striped(const lpath& path) {
    stripe_stats c;

    apply(
        path,
        [&c](const lpath& p) -> util::expected<void, std::string> {
            if (p.is_regular_file() && p.path.filename() == "store.squashfs") {
                // files always have an explicit count >= 1
                if (p.config.count > 1) {
                    c.files.yes++;
                    spdlog::debug("lustre::is_striped YES {}", p);
                } else {
                    c.files.no++;
                    spdlog::debug("lustre::is_striped NO  {}", p);
                }
            } else if (p.is_directory()) {
                // count==-1 on a directory implies "stripe contents over all
                // OST"
                if (p.config.count < 0 || p.config.count > 1) {
                    c.directories.yes++;
                    spdlog::debug("lustre::is_striped YES {}", p);
                } else {
                    c.directories.no++;
                    spdlog::debug("lustre::is_striped NO  {}", p);
                }
            }
            return {};
        },
        true);

    return c;
}

} // namespace lustre
