#include <any>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>

#include <uenv/parse.h>
#include <uenv/repository.h>
#include <uenv/uenv.h>
#include <util/expected.h>

#include <fmt/core.h>
#include <fmt/ranges.h>

// the C API for sqlite3
#include <sqlite3.h>

namespace fs = std::filesystem;

namespace uenv {

template <typename T> using hopefully = util::expected<T, std::string>;

using util::unexpected;

/// get the default location for the user's repository.
/// - use the environment variable UENV_REPO_PATH if it is set
/// - use $SCRATCH/.uenv/repo if $SCRATCH is set
/// - use $HOME/.uenv/repo if $HOME is set
///
/// returns nullopt if no environment variables were set.
/// returns error if
/// - the provided path is not absolute
/// - the path string was not valid
util::expected<std::optional<std::string>, std::string> default_repo_path() {
    std::string path_string;
    if (auto p = std::getenv("UENV_REPO_PATH")) {
        return p;
    } else if (auto p = std::getenv("SCRATCH")) {
        return std::string(p) + "/.uenv-images";
    } else if (auto p = std::getenv("HOME")) {
        return std::string(p) + "/.uenv/repo";
    }
    return std::nullopt;
}

util::expected<std::filesystem::path, std::string>
validate_repo_path(const std::string& path, bool is_absolute, bool exists) {
    auto parsed_path_string = parse_path(path);
    if (!parsed_path_string) {
        return util::unexpected(
            fmt::format("{} is an invalid uenv repository path: {}", path,
                        parsed_path_string.error().message()));
    }
    try {
        const auto p = std::filesystem::path(*parsed_path_string);
    } catch (...) {
        return util::unexpected(
            fmt::format("{} is an invalid uenv repository path", path));
    }

    const auto p = fs::path(path);
    if (is_absolute && !p.is_absolute()) {
        return unexpected(fmt::format("'{}' is not an absolute path.", path));
    }
    if (exists && !fs::exists(p)) {
        return unexpected(fmt::format("'{}' does not exists.", path));
    }
    return fs::absolute(p);
}

// A thin wrapper around sqlite3*
// A shared pointer with a custom destructor that calls the sqlite3 C API
// descructor is used to manage the lifetime of the sqlite3* object. The shared
// pointer is used because the sqlite_statement type needs to hold a re
struct sqlite_database {
    // type definitions
    using db_ptr_type = std::shared_ptr<sqlite3>;

    // constructors
    sqlite_database(sqlite3* db) : data(db, &sqlite3_close) {
    }
    sqlite_database(sqlite_database&&) = default;

    // state
    db_ptr_type data;
};

hopefully<sqlite_database> open_sqlite_db(const fs::path path) {
    sqlite3* db;
    if (sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READONLY,
                        NULL) != SQLITE_OK) {
        return unexpected(fmt::format(
            "internal sqlite3 error opening database file {}", path.string()));
    }

    return sqlite_database(db);
}

struct sqlite_statement {
    // type definitions
    using db_ptr_type = sqlite_database::db_ptr_type;
    using stmt_ptr_type =
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

    struct column {
        int index;
        std::string name;
        std::any value;

        operator std::int64_t() const {
            return std::any_cast<std::int64_t>(value);
        }
        operator std::size_t() const {
            return std::any_cast<std::int64_t>(value);
        }
        operator std::string() const {
            return std::any_cast<std::string>(value);
        }
    };

    // constructors
    sqlite_statement(std::string query, sqlite_database& db,
                     sqlite3_stmt* statement)
        : query(std::move(query)), db(db.data),
          statement(statement, &sqlite3_finalize) {
    }

    // functions
    bool step() {
        return SQLITE_ROW == sqlite3_step(statement.get());
    }

    hopefully<column> operator[](int i) const {
        if (i >= sqlite3_column_count(statement.get()) || i < 0) {
            return unexpected(fmt::format("sqlite3_column_count out of range"));
        }

        column result;
        result.index = i;
        result.name = sqlite3_column_name(statement.get(), i);
        std::string_view type = sqlite3_column_decltype(statement.get(), i);
        if (type == "TEXT") {
            result.value = std::string(reinterpret_cast<const char*>(
                sqlite3_column_text(statement.get(), i)));
        } else if (type == "INTEGER") {
            // sqlite3 stores integers in the database using between 1-8 bytes
            // (according to the size of the value) but when they are loaded
            // into memory, it always stores them as a signed 8 byte value.
            // So always convert from int64.
            result.value = static_cast<std::int64_t>(
                sqlite3_column_int64(statement.get(), i));
        } else {
            return unexpected(fmt::format("unknown column type {}", type));
        }

        return result;
    }

    hopefully<column> operator[](const std::string& name) const {
        int num_cols = sqlite3_column_count(statement.get());

        for (int col = 0; col < num_cols; ++col) {
            if (sqlite3_column_name(statement.get(), col) == name) {
                return operator[](col);
            }
        }

        return unexpected(fmt::format(
            "sqlite3 statement does not have a column named '{}'", name));
    }

    // state
    const std::string query;
    db_ptr_type db;
    stmt_ptr_type statement;
};

hopefully<sqlite_statement> create_sqlite_statement(const std::string& query,
                                                    sqlite_database& db) {
    sqlite3_stmt* statement;
    sqlite3* D = db.data.get();
    int rc = sqlite3_prepare_v2(D, query.c_str(), query.size() + 1, &statement,
                                nullptr);
    if (SQLITE_OK != rc) {
        return unexpected(
            fmt::format("unable to create prepared statement:\n{}\n\n{}",
                        sqlite3_errstr(rc), sqlite3_errmsg(D)));
    }

    return sqlite_statement{query, db, statement};
}

struct repository_impl {
    repository_impl(sqlite_database db, fs::path path, fs::path db_path)
        : db(std::move(db)), path(std::move(path)),
          db_path(std::move(db_path)) {
    }
    repository_impl(repository_impl&&) = default;
    sqlite_database db;
    fs::path path;
    fs::path db_path;

    util::expected<std::vector<uenv_record>, std::string>
    query(const uenv_label& label);
};

repository::repository(repository&&) = default;
repository::repository(std::unique_ptr<repository_impl> impl)
    : impl_(std::move(impl)) {
}

util::expected<repository, std::string>
open_repository(const fs::path& repo_path) {
    auto db_path = repo_path / "index.db";
    if (!fs::is_regular_file(db_path)) {
        return unexpected(fmt::format("the repository is invalid - the index "
                                      "database {} does not exist",
                                      db_path.string()));
    }

    // open the sqlite database
    auto db = open_sqlite_db(db_path);
    if (!db) {
        return unexpected(db.error());
    }

    return repository(
        std::make_unique<repository_impl>(std::move(*db), repo_path, db_path));
}

util::expected<std::vector<uenv_record>, std::string>
repository_impl::query(const uenv_label& label) {
    std::vector<uenv_record> results;

    std::string query = fmt::format("SELECT * FROM records");
    std::vector<std::string> query_terms;
    if (label.name) {
        query_terms.push_back(fmt::format("name  = '{}'", *label.name));
    }
    if (label.tag) {
        query_terms.push_back(fmt::format("tag  = '{}'", *label.tag));
    }
    if (label.version) {
        query_terms.push_back(fmt::format("version  = '{}'", *label.version));
    }
    if (label.uarch) {
        query_terms.push_back(fmt::format("uarch  = '{}'", *label.uarch));
    }
    if (label.system) {
        query_terms.push_back(fmt::format("system  = '{}'", *label.system));
    }

    if (!query_terms.empty()) {
        query += fmt::format(" WHERE {}", fmt::join(query_terms, " AND "));
    }

    auto s = create_sqlite_statement(query, db);
    if (!s) {
        return unexpected(
            fmt::format("creating database query: {}", s.error()));
    }
    while (s->step()) {
        // unsafe: unwrap using .value() without checking for errors.
        // the best way to do this would be to "validate" the database
        // beforehand by checking the columns exist. Even better, validate that
        // column 0 -> 'system', etc, and use integer indexes to look up more
        // effiently.
        results.push_back({(*s)["system"].value(), (*s)["uarch"].value(),
                           (*s)["name"].value(), (*s)["version"].value(),
                           (*s)["tag"].value(), (*s)["date"].value(),
                           (*s)["size"].value(), (*s)["sha256"].value(),
                           (*s)["id"].value()});
    }

    return results;
}

// wrapping the pimpled implementation

repository::~repository() = default;

const fs::path& repository::path() const {
    return impl_->path;
}

const fs::path& repository::db_path() const {
    return impl_->db_path;
}

util::expected<std::vector<uenv_record>, std::string>
repository::query(const uenv_label& label) {
    return impl_->query(label);
}

} // namespace uenv
