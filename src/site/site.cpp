#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <fmt/format.h>
#include <fmt/std.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <uenv/parse.h>
#include <uenv/repository.h>
#include <util/curl.h>
#include <util/envvars.h>
#include <util/expected.h>
#include <util/fs.h>
#include <util/sha.h>
#include <util/unique_fd.h>
#include <util/url.h>

#include "site.h"

namespace site {

namespace {

// a namespace name is used as a file name in the cache: accept only what the
// uenv parser accepts as a namespace, which excludes '/' and a leading '.'
bool is_namespace(const std::string& nspace) {
    auto parse = uenv::parse_uenv_nslabel(nspace + "::");
    return parse && parse->nspace == nspace;
}

// Whether `path` was modified within `age` of now. A time further than `age`
// in the future, from a clock that was wrong, is not within it, so that it
// can't keep a file recent for ever.
bool modified_within(const std::filesystem::path& path,
                     std::filesystem::file_time_type::duration age) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto modified = fs::last_write_time(path, ec);
    if (ec) {
        return false;
    }
    const auto elapsed = fs::file_time_type::clock::now() - modified;
    return elapsed < age && elapsed > -age;
}

// The contents of a regular file of at most `max_size` bytes. Nothing else is
// read: a FIFO put in place of the file would otherwise block the reader.
std::optional<std::string> read_cache_file(const std::filesystem::path& path,
                                           std::uintmax_t max_size) {
    util::unique_fd fd(::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
    struct stat st{};
    if (!fd || ::fstat(fd.get(), &st) != 0 || !S_ISREG(st.st_mode) ||
        static_cast<std::uintmax_t>(st.st_size) > max_size) {
        return std::nullopt;
    }
    std::string contents(static_cast<std::size_t>(st.st_size), '\0');
    std::size_t done = 0;
    while (done < contents.size()) {
        auto n =
            ::read(fd.get(), contents.data() + done, contents.size() - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return std::nullopt;
        }
        done += static_cast<std::size_t>(n);
    }
    return contents;
}

// create `path` if it does not exist, and set its modification time to now
bool touch(const std::filesystem::path& path) {
    util::unique_fd fd(
        ::open(path.c_str(),
               O_WRONLY | O_CREAT | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC, 0644));
    return fd && ::futimens(fd.get(), nullptr) == 0;
}

} // namespace

util::expected<std::vector<uenv::uenv_record>, std::string>
parse_registry_listing(std::string_view body, const std::string& nspace) {
    using json = nlohmann::json;

    std::vector<uenv::uenv_record> records;
    try {
        auto raw = json::parse(body);

        for (auto& j : raw["results"]) {
            const std::string sha = j["sha256"];
            const auto date = uenv::parse_uenv_date(j["created"]);
            auto rg = uenv::parse_registry_entry(j["path"]);
            if (!rg) {
                spdlog::warn("drop due to error: {}", rg.error().message());
            } else if (!date) {
                spdlog::warn("drop {} due to invalid date: {}", rg.value(),
                             date.error().message());
            } else if (rg->nspace == nspace) {
                auto sha_value = util::sha256::parse(sha);
                auto id_value = util::uenv_id::parse(sha.substr(0, 16));
                if (!sha_value || !id_value) {
                    spdlog::warn("drop due to invalid sha256 '{}'", sha);
                    continue;
                }
                spdlog::trace("keep {} {}", sha.substr(0, 16), *rg);
                records.push_back({
                    .system = rg->system,
                    .uarch = rg->uarch,
                    .name = rg->name,
                    .version = rg->version,
                    .tag = rg->tag,
                    .date = *date,
                    .size_byte = j["size"],
                    .sha = *sha_value,
                    .id = *id_value,
                });
            } else {
                spdlog::trace("drop {} {}", sha.substr(0, 16), *rg);
            }
        }
    } catch (std::exception& e) {
        return util::unexpected(fmt::format("{}", e.what()));
    }

    return records;
}

util::expected<uenv::repository, std::string>
registry_listing(const std::optional<util::url>& listing_url,
                 const std::string& nspace,
                 const std::optional<std::filesystem::path>& cache_root) {
    // perform curl call against middleware end point
    // example of full url end point call:
    //   https://uenv-list.svc.cscs.ch/list?namespace=deploy&cluster=todi&arch=gh200&app=prgenv-gnu&version=24.7
    // we only filter on namespace, and use the database to do more querying
    // later. the base URL can be overridden via registry.listing_url (e.g. to a
    // local mock endpoint for testing).
    const auto base =
        listing_url.value_or(*util::parse_url(default_listing_url));
    // query_param picks the separator and encodes, so a listing_url that
    // already carries a query no longer grows a second '?'.
    const auto url = base.query_param("namespace", nspace).string();
    spdlog::debug("registry_listing: {}", url);
    auto raw_records = util::curl::get(url);

    if (!raw_records) {
        int ec = raw_records.error().code;
        spdlog::error("curl error {}: {}", ec, raw_records.error().message);
        return util::unexpected{fmt::format(
            "unable to reach {} to get list of available uenv", base.string())};
    }

    auto records = parse_registry_listing(*raw_records, nspace);
    if (!records) {
        spdlog::error("error results returned from uenv listing: {}",
                      records.error());
        return util::unexpected(records.error());
    }

    if (cache_root) {
        save_registry_listing(listing_cache_dir(*cache_root, listing_url),
                              nspace, *raw_records);
    }

    //   generate list of records from json
    auto store = uenv::create_repository();
    for (auto r : records.value()) {
        store->add(r);
    }

    spdlog::debug("registry_listing: {} records found in namespace {}",
                  records->size(), nspace);

    return store;
}

std::filesystem::path
listing_cache_dir(const std::filesystem::path& cache_root,
                  const std::optional<util::url>& listing_url) {
    const auto url =
        listing_url ? listing_url->string() : std::string(default_listing_url);
    return cache_root / "listing" /
           util::sha256_string(url).string().substr(0, 16);
}

void save_registry_listing(const std::filesystem::path& dir,
                           const std::string& nspace, std::string_view body) {
    namespace fs = std::filesystem;

    if (!is_namespace(nspace)) {
        return;
    }
    if (auto r = util::ensure_directory(dir); !r) {
        spdlog::debug("unable to cache the listing: {}", r.error());
        return;
    }
    const auto file = dir / fmt::format("{}.json", nspace);
    const auto tmp = dir / fmt::format(".{}.json.{}", nspace, getpid());
    bool written = false;
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        out.close();
        written = !out.fail();
    }
    std::error_code ec;
    if (written) {
        fs::rename(tmp, file, ec);
        // only a directory in the way stops the rename: replace it, so that no
        // state of the cache needs to be repaired by hand
        if (ec && fs::is_directory(fs::symlink_status(file))) {
            fs::remove_all(file, ec);
            fs::rename(tmp, file, ec);
        }
    }
    if (!written || ec) {
        spdlog::debug("unable to cache the listing in {}", file);
        fs::remove(tmp, ec);
        return;
    }
    spdlog::debug("cached the listing of {} in {}", nspace, file);
}

std::optional<std::vector<uenv::uenv_record>>
cached_registry_listing(const std::filesystem::path& dir,
                        const std::string& nspace) {
    if (!is_namespace(nspace)) {
        return std::nullopt;
    }
    const auto file = dir / fmt::format("{}.json", nspace);
    if (!modified_within(file, listing_cache_max_age)) {
        return std::nullopt;
    }
    auto body = read_cache_file(file, listing_cache_max_size);
    if (!body) {
        return std::nullopt;
    }
    auto records = parse_registry_listing(*body, nspace);
    if (!records) {
        return std::nullopt;
    }
    return std::move(*records);
}

std::vector<std::string> cached_namespaces(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;

    std::vector<std::string> result;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end;
         it.increment(ec)) {
        const auto& path = it->path();
        if (path.extension() != ".json") {
            continue;
        }
        auto nspace = path.stem().string();
        if (is_namespace(nspace)) {
            result.push_back(std::move(nspace));
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool claim_listing_refresh(const std::filesystem::path& dir,
                           const std::string& nspace, bool usable) {
    namespace fs = std::filesystem;

    if (!is_namespace(nspace)) {
        return false;
    }
    if (usable && modified_within(dir / fmt::format("{}.json", nspace),
                                  listing_refresh_interval)) {
        return false;
    }
    const auto stamp = dir / fmt::format(".{}.refresh", nspace);
    if (modified_within(stamp, listing_refresh_interval)) {
        return false;
    }
    if (!util::ensure_directory(dir)) {
        return false;
    }
    if (touch(stamp)) {
        return true;
    }
    // something other than a file is in the way (a directory, a FIFO or a
    // symbolic link): replace it, so that no state of the cache needs to be
    // repaired by hand
    std::error_code ec;
    if (fs::exists(fs::symlink_status(stamp, ec)) &&
        !fs::is_regular_file(fs::symlink_status(stamp, ec))) {
        fs::remove_all(stamp, ec);
        return touch(stamp);
    }
    return false;
}

void refresh_registry_listing(const std::optional<util::url>& listing_url,
                              const std::string& nspace,
                              const std::filesystem::path& cache_root) {
    namespace fs = std::filesystem;

    // the temporary files of save_registry_listing, .<nspace>.json.<pid>
    const auto dir = listing_cache_dir(cache_root, listing_url);
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end;
         it.increment(ec)) {
        const auto name = it->path().filename().string();
        if (name.starts_with('.') && name.find(".json.") != std::string::npos &&
            !modified_within(it->path(), std::chrono::hours(1))) {
            std::error_code remove_ec;
            fs::remove(it->path(), remove_ec);
        }
    }
    (void)registry_listing(listing_url, nspace, cache_root);
}

std::optional<std::string> get_system_name(const envvars::state& calling_env) {
    if (auto name = calling_env.get("CLUSTER_NAME")) {
        spdlog::debug("cluster name is '{}'", name.value());
        return name.value();
    }

    spdlog::debug("cluster name is undefined");

    return std::nullopt;
}

} // namespace site
