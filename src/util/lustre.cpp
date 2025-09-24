#include <charconv>
#include <filesystem>

#include <sys/vfs.h>

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

std::optional<std::int64_t> run_lfs(const std::vector<std::string> args) {
    auto result = util::run(args);

    if (!result) {
        return std::nullopt;
    }
    if (!result->wait()) {
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
        return value;
    }

    return std::nullopt;
}

util::expected<status, error> getstripe(const std::filesystem::path& p,
                                        const envvars::state& env) {
    if (!is_lustre(p)) {
        return util::unexpected{error::not_lustre};
    }

    auto lfs = util::which("lfs", env.get("PATH").value_or(""));
    if (!lfs) {
        return util::unexpected{error::no_lfs};
    }

    status s;

    // lfs getstripe --stripe-count $path
    if (const auto value = run_lfs(
            {lfs->string(), "getstripe", "--stripe-count", p.string()})) {
        s.count = *value;
    } else {
        return util::unexpected{error::lfs};
    }

    // lfs getstripe --stripe-size $path
    if (const auto value = run_lfs(
            {lfs->string(), "getstripe", "--stripe-size", p.string()})) {
        s.size = *value;
    } else {
        return util::unexpected{error::lfs};
    }

    // lfs getstripe --stripe-index $path
    if (const auto value = run_lfs(
            {lfs->string(), "getstripe", "--stripe-size", p.string()})) {
        s.index = *value;
    } else {
        return util::unexpected{error::lfs};
    }

    return s;
}

bool setstripe(const std::filesystem::path& p, const status&,
               const envvars::state& env);

} // namespace lustre
