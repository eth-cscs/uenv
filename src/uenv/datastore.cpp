// #include <expected>
#include <filesystem>
#include <memory>
#include <vector>

#include <uenv/datastore.h>
#include <uenv/uenv.h>
#include <util/expected.h>

#include <fmt/core.h>

// the C API for sqlite3
#include <sqlite3.h>

namespace fs = std::filesystem;

namespace uenv {

template <typename T> using hopefully = util::expected<T, std::string>;
using util::unexpected;

struct sqlite3db {
    using db_ptr_type = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
    db_ptr_type db;

    sqlite3db(sqlite3* db) : db(db, &sqlite3_close) {
    }
    sqlite3db& operator=(sqlite3db&&) = default;
    sqlite3db(sqlite3db&&) = default;
    sqlite3db& operator=(const sqlite3db&) = delete;
    sqlite3db(const sqlite3db&) = delete;
};

hopefully<sqlite3db> open_sqlite_db(const fs::path path) {
    sqlite3* db;
    if (sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READONLY,
                        NULL) != SQLITE_OK) {
        return unexpected(fmt::format(
            "internal sqlite3 error opening database file {}", path.string()));
    }

    return sqlite3db(db);
}

struct datastore_impl {
    datastore_impl(sqlite3db db, fs::path path, fs::path db_path)
        : db(std::move(db)), path(std::move(path)),
          db_path(std::move(db_path)) {
    }
    datastore_impl(datastore_impl&&) = default;
    sqlite3db db;
    fs::path path;
    fs::path db_path;

    util::expected<std::vector<uenv_record>, std::string>
    query(const uenv_label& label) const;
};

datastore::datastore(datastore&&) = default;
datastore::datastore(std::unique_ptr<datastore_impl> impl)
    : impl_(std::move(impl)) {
}

util::expected<datastore, std::string>
open_datastore(const fs::path& repo_path) {
    auto db_path = repo_path / "index.db";
    if (!fs::is_regular_file(db_path)) {
        return unexpected(fmt::format(
            "the repository is invalid - the index database {} does not exist",
            db_path.string()));
    }

    // open the sqlite database
    auto db = open_sqlite_db(db_path);
    if (!db) {
        return unexpected(db.error());
    }

    return datastore(
        std::make_unique<datastore_impl>(std::move(*db), repo_path, db_path));
}

util::expected<std::vector<uenv_record>, std::string>
datastore_impl::query(const uenv_label& label) const {
    std::vector<uenv_record> result;

    return result;
}

datastore::~datastore() = default;

const fs::path& datastore::path() const {
    return impl_->path;
}

const fs::path& datastore::db_path() const {
    return impl_->db_path;
}

util::expected<std::vector<uenv_record>, std::string>
datastore::query(const uenv_label& label) const {
    return impl_->query(label);
}

} // namespace uenv
