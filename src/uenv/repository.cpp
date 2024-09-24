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
#include <spdlog/spdlog.h>

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
        return unexpected(fmt::format("'{}' does not exist.", path));
    }
    return fs::absolute(p);
}

// A thin wrapper around sqlite3*
// A shared pointer with a custom destructor that calls the sqlite3 C API
// descructor is used to manage the lifetime of the sqlite3* object. The shared
// pointer is used because the statement type needs to hold a re
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

hopefully<sqlite_database> open_sqlite_database(const fs::path path,
                                                repo_mode mode) {
    using enum repo_mode;

    int flags = mode == readonly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE;
    sqlite3* db;
    if (sqlite3_open_v2(path.string().c_str(), &db, flags, NULL) != SQLITE_OK) {
        return unexpected(fmt::format(
            "internal sqlite3 error opening database file {}", path.string()));
    }
    // double check that the database can be written if in readwrite mode
    if (mode == readwrite && sqlite3_db_readonly(db, "main") == 1) {
        // close the database before returning an error
        sqlite3_close(db);
        return unexpected(
            fmt::format("the repo {} is read only", path.string()));
    }

    return sqlite_database(db);
}

struct statement {
    // type definitions
    using db_ptr_type = sqlite_database::db_ptr_type;
    using stmt_ptr_type =
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

    // constructor
    statement(std::string text, sqlite_database& db, sqlite3_stmt* statement)
        : text(std::move(text)), db(db.data),
          wrap(statement, &sqlite3_finalize) {
    }

    // perform the statement.
    // return false if there was an error
    bool step() {
        return SQLITE_OK == sqlite3_step(wrap.get());
    }

    // state

    // the text of the statement, e.g. "SELECT * FROM mytable"
    const std::string text;
    // the underlying database object
    db_ptr_type db;
    // the underlying statement object
    stmt_ptr_type wrap;
};

// extend the statement interface for queries
struct query : private statement {
    using statement::statement;

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

    // functions
    bool step() {
        return SQLITE_ROW == sqlite3_step(wrap.get());
    }

    hopefully<column> operator[](int i) const {
        if (i >= sqlite3_column_count(wrap.get()) || i < 0) {
            return unexpected(fmt::format("sqlite3_column_count out of range"));
        }

        column result;
        result.index = i;
        result.name = sqlite3_column_name(wrap.get(), i);
        std::string_view type = sqlite3_column_decltype(wrap.get(), i);
        if (type == "TEXT") {
            result.value = std::string(reinterpret_cast<const char*>(
                sqlite3_column_text(wrap.get(), i)));
        } else if (type == "INTEGER") {
            // sqlite3 stores integers in the database using between 1-8 bytes
            // (according to the size of the value) but when they are loaded
            // into memory, it always stores them as a signed 8 byte value.
            // So always convert from int64.
            result.value =
                static_cast<std::int64_t>(sqlite3_column_int64(wrap.get(), i));
        } else {
            return unexpected(fmt::format("unknown column type {}", type));
        }

        return result;
    }

    hopefully<column> operator[](const std::string& name) const {
        int num_cols = sqlite3_column_count(wrap.get());

        for (int col = 0; col < num_cols; ++col) {
            if (sqlite3_column_name(wrap.get(), col) == name) {
                return operator[](col);
            }
        }

        return unexpected(fmt::format(
            "sqlite3 query does not have a column named '{}'", name));
    }
};

hopefully<sqlite3_stmt*> create_sqlite3_stmt(const std::string& text,
                                             sqlite_database& db) {
    sqlite3_stmt* ptr;
    sqlite3* D = db.data.get();
    int rc =
        sqlite3_prepare_v2(D, text.c_str(), text.size() + 1, &ptr, nullptr);
    if (SQLITE_OK != rc) {
        return unexpected(
            fmt::format("unable to create prepared statement:\n{}\n\n{}",
                        sqlite3_errstr(rc), sqlite3_errmsg(D)));
    }

    return ptr;
}

hopefully<statement> create_statement(const std::string& text,
                                      sqlite_database& db) {
    auto ptr = create_sqlite3_stmt(text, db);
    if (!ptr) {
        return unexpected(ptr.error());
    }

    return statement{text, db, *ptr};
}

// create a statement and execute it.
// assumes that the statement will return success after one step
// - use a query type for queries that are stepped once for each return row of
// the database
hopefully<void> exec_statement(const std::string& text, sqlite_database& db) {
    auto stmnt = create_statement(text, db);
    if (!stmnt) {
        return unexpected(stmnt.error());
    }
    if (!stmnt->step()) {
        return unexpected(
            fmt::format("error executing the SQL statment {}", text));
    }
    return {};
}

hopefully<query> create_query(const std::string& text, sqlite_database& db) {
    auto ptr = create_sqlite3_stmt(text, db);
    if (!ptr) {
        return unexpected(ptr.error());
    }

    return query{text, db, *ptr};
}

struct repository_impl {
    repository_impl(sqlite_database db, fs::path path, fs::path db_path,
                    bool readonly = true)
        : db(std::move(db)), path(std::move(path)), db_path(std::move(db_path)),
          is_readonly(readonly) {
    }
    repository_impl(repository_impl&&) = default;
    sqlite_database db;
    fs::path path;
    fs::path db_path;

    util::expected<std::vector<uenv_record>, std::string>
    query(const uenv_label&);

    util::expected<void, std::string> exec(const std::string&);
    util::expected<int, std::string> add_uenv(const uenv_record&);

    const bool is_readonly;
};

repository::repository(repository&&) = default;
repository::repository(std::unique_ptr<repository_impl> impl)
    : impl_(std::move(impl)) {
}

util::expected<repository, std::string>
open_repository(const fs::path& repo_path, repo_mode mode) {
    using enum repo_mode;
    auto db_path = repo_path / "index.db";
    if (!fs::is_regular_file(db_path)) {
        return unexpected(fmt::format("the repository is invalid - the index "
                                      "database {} does not exist",
                                      db_path.string()));
    }

    // open the sqlite database
    auto db = open_sqlite_database(db_path, mode);
    if (!db) {
        return unexpected(db.error());
    }

    return repository(std::make_unique<repository_impl>(
        std::move(*db), repo_path, db_path, true));
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

    // spdlog::info("running database query\n{}", query);
    auto s = create_query(query, db);
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
                           (*s)["size"].value(), sha256((*s)["sha256"].value()),
                           uenv_id((*s)["id"].value())});
    }

    // now check for id and sha search terms
    if (label.only_name()) {
        // search for an if name could also be an id
        if (is_sha(*label.name, 16)) {
            auto result = create_query(
                fmt::format("SELECT * FROM records WHERE id = '{}'",
                            uenv_id(*label.name).string()),
                db);
            if (result) {
                while (result->step()) {
                    results.push_back(
                        {(*result)["system"].value(),
                         (*result)["uarch"].value(), (*result)["name"].value(),
                         (*result)["version"].value(), (*result)["tag"].value(),
                         (*result)["date"].value(), (*result)["size"].value(),
                         sha256((*result)["sha256"].value()),
                         uenv_id((*result)["id"].value())});
                }
            }
        }
        // search for a sha if name could also be a sha256
        else if (is_sha(*label.name, 64)) {
            auto result = create_query(
                fmt::format("SELECT * FROM records WHERE sha256 = '{}'",
                            sha256(*label.name).string()),
                db);
            if (result) {
                while (result->step()) {
                    results.push_back(
                        {(*result)["system"].value(),
                         (*result)["uarch"].value(), (*result)["name"].value(),
                         (*result)["version"].value(), (*result)["tag"].value(),
                         (*result)["date"].value(), (*result)["size"].value(),
                         sha256((*result)["sha256"].value()),
                         uenv_id((*result)["id"].value())});
                }
            }
        }
    }

    // sort the results
    std::sort(results.begin(), results.end());
    // remove duplicates
    results.erase(std::unique(results.begin(), results.end()), results.end());

    return results;
}

// schema

std::vector<std::string> schema_tables() {
    return {
        R"(CREATE TABLE images (
            sha256 TEXT PRIMARY KEY CHECK(length(sha256)==64),
            id TEXT UNIQUE CHECK(length(id)==16),
            date TEXT NOT NULL,
            size INTEGER NOT NULL);
        )",
        R"(CREATE TABLE uenv (
            version_id INTEGER PRIMARY KEY,
            system TEXT NOT NULL,
            uarch TEXT NOT NULL,
            name TEXT NOT NULL,
            version TEXT NOT NULL,
            UNIQUE (system, uarch, name, version));
        )",
        R"(CREATE TABLE tags (
            version_id INTEGER,
            tag TEXT NOT NULL,
            sha256 TEXT NOT NULL,
            PRIMARY KEY (version_id, tag),
            FOREIGN KEY (version_id)
                REFERENCES uenv (version_id)
                    ON DELETE CASCADE
                    ON UPDATE CASCADE,
            FOREIGN KEY (sha256)
                REFERENCES images (sha256)
                    ON DELETE CASCADE
                    ON UPDATE CASCADE);
        )",
        R"(CREATE VIEW records AS
            SELECT
                uenv.system  AS system,
                uenv.uarch   AS uarch,
                uenv.name      AS name,
                uenv.version   AS version,
                tags.tag       AS tag,
                images.date    AS date,
                images.size    AS size,
                tags.sha256    AS sha256,
                images.id      AS id
            FROM tags
                INNER JOIN uenv   ON uenv.version_id = tags.version_id
                INNER JOIN images ON images.sha256   = tags.sha256;
        )"};
}

// std::string_view schema() {
void schema(sqlite_database& db) {
    exec_statement("BEGIN;", db);
    exec_statement("PRAGMA foreign_keys=on;", db);
    for (const auto& table : schema_tables()) {
        exec_statement(table, db);
    }
    exec_statement("COMMIT;", db);
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

bool repository::is_readonly() const {
    return impl_->is_readonly;
}

} // namespace uenv
