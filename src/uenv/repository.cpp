#include <any>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>

#include <uenv/parse.h>
#include <uenv/repository.h>
#include <uenv/uenv.h>
#include <util/envvars.h>
#include <util/expected.h>
#include <util/fs.h>
#include <util/lustre.h>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
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
///
/// returns error if it a path is set, but it is invalid
std::optional<std::string> default_repo_path(const envvars::state& env) {
    std::optional<std::string> path_string;
    if (auto p = env.get("SCRATCH")) {
        spdlog::trace(fmt::format("default_repo_path: found SCRATCH={}", p));
        path_string = std::string(p.value()) + "/.uenv-images";
    } else {
        if (auto p = env.get("HOME")) {
            spdlog::trace(fmt::format("default_repo_path: found HOME={}", p));
            path_string = std::string(p.value()) + "/.uenv/repo";
        } else {
            spdlog::trace("default_repo_path: no default location found");
        }
    }
    return path_string;
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
// A shared pointer with a custom destructor that calls the sqlite3 C
// API descructor is used to manage the lifetime of the sqlite3* object.
// The shared pointer is used because the statement type needs to hold a
// re
struct sqlite_database {
    // type definitions
    using db_ptr_type = std::shared_ptr<sqlite3>;

    // constructors
    sqlite_database(sqlite3* db) : data(db, &sqlite3_close) {
    }
    sqlite_database(sqlite_database&&) = default;

    std::string error() {
        return sqlite3_errmsg(data.get());
    }

    // state
    db_ptr_type data;
};

hopefully<sqlite_database> open_sqlite_database(const fs::path path,
                                                repo_mode mode) {
    using enum repo_mode;

    if (!fs::exists(path)) {
        return unexpected(
            fmt::format("database file {} does not exist", path.string()));
    }

    const int flags =
        mode == readonly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE;
    spdlog::debug("open_sqlite_database: attempting to open {} in {} mode.",
                  path, mode == readonly ? "readonly" : "readwrite");
    sqlite3* db;
    if (sqlite3_open_v2(path.string().c_str(), &db, flags, NULL) != SQLITE_OK) {
        return unexpected(fmt::format("did not open database file {}: {}",
                                      path.string(), sqlite3_errmsg(db)));
    }
    spdlog::info("open_sqlite_database: {}", path);

    // double check that the database can be written if in readwrite mode
    if (mode == readwrite && sqlite3_db_readonly(db, "main") == 1) {
        spdlog::error("open_sqlite_database: {} was opened read only.", path);
        // close the database before returning an error
        sqlite3_close(db);
        return unexpected(
            fmt::format("the repo {} is read only", path.string()));
    }

    return sqlite_database(db);
}

// Takes a single argument which is the path of the database to create.
// If path is not set, an in memory database is created.
hopefully<sqlite_database>
create_sqlite_database(const std::optional<fs::path> path = {}) {
    using enum repo_mode;

    if (path && fs::exists(*path)) {
        return unexpected(
            fmt::format("database file {} already exists", path->string()));
    }
    if (path && !fs::exists(path->parent_path())) {
        return unexpected(fmt::format("the path {} needs to be crated first",
                                      path->parent_path().string()));
    }

    sqlite3* db;
    if (path) {
        const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
        if (sqlite3_open_v2(path->string().c_str(), &db, flags, NULL) !=
            SQLITE_OK) {
            return unexpected(
                fmt::format("unable to create database file {}: {}",
                            path->string(), sqlite3_errmsg(db)));
        }
        spdlog::info("create_sqlite3_database: created db {}", path);
    } else {
        const int flags =
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_MEMORY;
        if (sqlite3_open_v2("in-memory", &db, flags, NULL) != SQLITE_OK) {
            return unexpected(fmt::format(
                "unable to create in memory database: {}", sqlite3_errmsg(db)));
        }
        spdlog::info("create_sqlite_database: created in-memory db");
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
        return SQLITE_DONE == sqlite3_step(wrap.get());
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
            // sqlite3 stores integers in the database using between 1-8
            // bytes (according to the size of the value) but when they
            // are loaded into memory, it always stores them as a signed
            // 8 byte value. So always convert from int64.
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
// - use a query type for queries that are stepped once for each return
// row of the database
hopefully<void> exec_statement(const std::string& text, sqlite_database& db) {
    spdlog::trace("exec_statement: {}", text);
    auto stmnt = create_statement(text, db);
    if (!stmnt) {
        return unexpected(stmnt.error());
    }
    if (!stmnt->step()) {
        return unexpected(
            fmt::format("SQL error '{}' executing {}", db.error(), text));
    }
    return {};
}

hopefully<query> create_query(const std::string& text, sqlite_database& db) {
    spdlog::trace("create_query: {}", text);
    auto ptr = create_sqlite3_stmt(text, db);
    if (!ptr) {
        return unexpected(ptr.error());
    }

    return query{text, db, *ptr};
}

struct repository_impl {
    repository_impl(sqlite_database db, std::optional<fs::path> path,
                    bool readonly = true)
        : db(std::move(db)), path(std::move(path)), is_readonly(readonly) {
    }
    repository_impl(repository_impl&&) = default;
    mutable sqlite_database db;
    std::optional<fs::path> path;

    util::expected<std::vector<uenv_record>, std::string>
    query(const uenv_label&) const;
    repository::pathset uenv_paths(sha256) const;

    bool contains(const uenv_record&) const;

    util::expected<void, std::string> add(const uenv_record&);
    util::expected<record_set, std::string> remove(const uenv_record&);
    util::expected<record_set, std::string> remove(const sha256&);

    const bool is_readonly;
};

repository::repository(repository&&) = default;
repository::repository(std::unique_ptr<repository_impl> impl)
    : impl_(std::move(impl)) {
}

repo_state validate_repository(const fs::path& repo_path) {
    using enum repo_state;

    if (!fs::is_directory(repo_path)) {
        spdlog::debug("validate_repository: repository path {} does not exist",
                      repo_path);
        return no_exist;
    }
    spdlog::debug("validate_repository: repository path {} exists", repo_path);

    auto db_path = repo_path / "index.db";
    if (!fs::is_regular_file(db_path)) {
        spdlog::debug("validate_repository: database {} does not exist",
                      db_path);
        // the path exists, but there is no database file
        return no_exist;
    }
    spdlog::debug("validate_repository: database {} exists", db_path);

    const auto level = std::min(util::file_access_level(repo_path),
                                util::file_access_level(db_path));
    spdlog::debug("validate_repository: status {}",
                  (level == util::file_level::none
                       ? "invalid"
                       : (level == util::file_level::readonly ? "read-only"
                                                              : "read-write")));
    switch (level) {
    case util::file_level::none:
        return invalid;
    case util::file_level::readonly:
        return readonly;
    case util::file_level::readwrite:
        return readwrite;
    }

    // all cases should be handled above
    return invalid;
}

util::expected<repository, std::string>
open_repository(const fs::path& repo_path, repo_mode mode) {
    using enum repo_mode;
    const auto initial_state = validate_repository(repo_path);

    switch (initial_state) {
    case repo_state::invalid:
        return unexpected(
            fmt::format("the repository {} is invalid", repo_path));
    case repo_state::no_exist:
        return unexpected(
            fmt::format("the repository {} does not exist", repo_path));
    case repo_state::readonly:
        if (mode == readwrite) {
            return unexpected(
                fmt::format("the repository {} is read only", repo_path));
        }
    default:
        break;
    }

    // open the sqlite database
    auto db_path = repo_path / "index.db";
    auto db = open_sqlite_database(db_path, mode);
    if (!db) {
        return unexpected(db.error());
    }

    return repository(
        std::make_unique<repository_impl>(std::move(*db), repo_path, true));
}

std::vector<std::string> schema_tables();

util::expected<repository, std::string>
create_repository(const fs::path& repo_path) {
    using enum repo_state;

    auto abs_repo_path = fs::absolute(repo_path);

    const auto initial_state = validate_repository(abs_repo_path);
    switch (initial_state) {
    case invalid:
        return unexpected(fmt::format(
            "unable to create repository: {} is invalid", abs_repo_path));
    case readonly:
    case readwrite:
        return unexpected(fmt::format(
            "unable to create repository: {} already exists", abs_repo_path));
    default:
        break;
    }

    spdlog::debug("creating repo path {}", abs_repo_path);
    {
        std::error_code ec;
        fs::create_directories(abs_repo_path, ec);
        if (ec) {
            spdlog::error("unable to create repository path: {}", ec.message());
            return unexpected("unable to create repository");
        }
    }

    // apply lustre striping to repository
    if (lustre::is_lustre(abs_repo_path)) {
        if (auto p = lustre::load_path(abs_repo_path, {})) {
            if (!lustre::is_striped(*p)) {
                lustre::set_striping(*p, lustre::default_striping);
            }
        }
    }

    auto db_path = abs_repo_path / "index.db";

    // open the sqlite database
    auto db = create_sqlite_database(db_path);
    if (!db) {
        spdlog::error("unable to create repository database: {}", db.error());
        return unexpected(fmt::format("unable to create repository"));
    }

    if (auto r = exec_statement("PRAGMA foreign_keys = ON", *db); !r) {
        spdlog::error(r.error());
        return unexpected("unable to create repository");
    }
    if (auto r = exec_statement("BEGIN", *db); !r) {
        spdlog::error(r.error());
        return unexpected("unable to create repository");
    }
    for (const auto& table : schema_tables()) {
        if (auto r = exec_statement(table, *db); !r) {
            spdlog::error(r.error());
            return unexpected("unable to create repository");
        }
    }
    if (auto r = exec_statement("COMMIT", *db); !r) {
        spdlog::error(r.error());
        return unexpected("unable to create repository");
    }

    return repository(
        std::make_unique<repository_impl>(std::move(*db), abs_repo_path, true));
}

util::expected<repository, std::string> create_repository() {
    using enum repo_state;

    // open the sqlite database
    auto db = create_sqlite_database();
    if (!db) {
        spdlog::error("unable to create repository database: {}", db.error());
        return unexpected(fmt::format("unable to create repository"));
    }

    if (auto r = exec_statement("PRAGMA foreign_keys = ON", *db); !r) {
        spdlog::error(r.error());
        return unexpected("unable to create repository");
    }
    if (auto r = exec_statement("BEGIN", *db); !r) {
        spdlog::error(r.error());
        return unexpected("unable to create repository");
    }
    for (const auto& table : schema_tables()) {
        if (auto r = exec_statement(table, *db); !r) {
            spdlog::error(r.error());
            return unexpected("unable to create repository");
        }
    }
    if (auto r = exec_statement("COMMIT", *db); !r) {
        spdlog::error(r.error());
        return unexpected("unable to create repository");
    }

    return repository(
        std::make_unique<repository_impl>(std::move(*db), std::nullopt, true));
}

repository::pathset repository_impl::uenv_paths(sha256 sha) const {
    namespace fs = std::filesystem;

    const auto lit = sha.string();

    const fs::path repo_root = path ? *path : fs::path(".");
    const fs::path repo_store_root = repo_root / "images";

    return {.root = repo_root,
            .store_root = repo_store_root,
            .store = repo_store_root / lit,
            .meta = repo_store_root / lit / "meta",
            .squashfs = repo_store_root / lit / "store.squashfs"};
}

util::expected<void, std::string>
repository_impl::add(const uenv::uenv_record& r) {
    std::vector<std::string> statements{
        // begin the transaction
        "PRAGMA foreign_keys = ON",
        "BEGIN",
        // insert the image information to images
        fmt::format("INSERT OR IGNORE INTO images (sha256, id, date, size) "
                    "VALUES ('{}', '{}', '{}', {})",
                    r.sha, r.id, r.date, r.size_byte),
        // insert the uenv information to uenv
        fmt::format("INSERT OR IGNORE INTO uenv (system, uarch, name, version) "
                    "VALUES ('{}', '{}', "
                    "'{}', '{}')",
                    r.system, r.uarch, r.name, r.version),
    };

    for (auto stmt : statements) {
        if (auto r = exec_statement(stmt, db); !r) {
            spdlog::error("repository_impl::add: {}", r.error());
            return unexpected("unable to update database");
        }
    }

    // Retrieve the version_id of the system/uarch/name/version identifier
    // This requires a SELECT query to get the correct version_id whether or not
    // a new row was added in the last INSERT
    std::int64_t version_id;
    {
        auto stmt = fmt::format(
            "SELECT version_id FROM uenv WHERE system = '{}' AND uarch = '{}' "
            "AND name = '{}' AND version = '{}'",
            r.system, r.uarch, r.name, r.version);
        auto result = create_query(stmt, db);
        result->step();

        version_id = (*result)["version_id"].value();
    }

    bool tag_exists;
    {
        auto stmt = fmt::format(
            "SELECT tag FROM tags WHERE version_id = '{}' AND tag = '{}'",
            version_id, r.tag);

        auto result = create_query(stmt, db);
        tag_exists = result->step();
    }

    auto stmt = tag_exists ? fmt::format("UPDATE tags SET sha256 = '{}' WHERE "
                                         "version_id = {} AND tag = '{}'",
                                         r.sha, version_id, r.tag)
                           : fmt::format("INSERT INTO tags (version_id, tag, "
                                         "sha256) VALUES ('{}', '{}', '{}')",
                                         version_id, r.tag, r.sha);

    if (auto r = exec_statement(stmt, db); !r) {
        spdlog::error("repository_impl::add: {}", r.error());
        return unexpected(r.error());
    }

    if (auto r = exec_statement("COMMIT", db); !r) {
        spdlog::error("repository_impl::add: {}", r.error());
        return unexpected(r.error());
    }

    return {};
}

util::expected<record_set, std::string>
repository_impl::remove(const sha256& sha) {
    auto matches = query({.name = sha.string()});
    std::vector<std::string> statements{
        // begin the transaction
        "PRAGMA foreign_keys = ON", "BEGIN",
        // delete the image from images
        fmt::format("DELETE FROM images WHERE sha256 = '{}'", sha.string()),
        "COMMIT"};

    for (auto stmt : statements) {
        if (auto r = exec_statement(stmt, db); !r) {
            spdlog::error("repository_impl::remove: {}", r.error());
            return unexpected("unable to update database");
        }
    }
    return *matches;
}

bool repository_impl::contains(const uenv_record& record) const {
    auto matches = query({.name = record.name,
                          .version = record.version,
                          .tag = record.tag,
                          .system = record.system,
                          .uarch = record.uarch});
    return !matches->empty();
}

util::expected<record_set, std::string>
repository_impl::remove(const uenv_record& record) {
    auto matches = query({.name = record.name,
                          .version = record.version,
                          .tag = record.tag,
                          .system = record.system,
                          .uarch = record.uarch});

    if (!matches->empty()) {
        // clang-format off
        std::vector<std::string> statements{
            // begin the transaction
            "BEGIN",
            "PRAGMA foreign_keys = ON",
            // delete the image from images
            // clang-format off
            fmt::format(
//WHERE sha256 = '{}' AND version_id IN ("
R"(
DELETE FROM tags
WHERE sha256='{}' AND tag='{}' AND version_id IN (
    SELECT version_id FROM uenv
    WHERE system='{}' AND uarch='{}' AND name='{}' AND version='{}')
)",
                record.sha, record.tag, record.system, record.uarch, record.name, record.version),
            "COMMIT"};
        // clang-format on

        for (auto stmt : statements) {
            if (auto r = exec_statement(stmt, db); !r) {
                spdlog::error("repository_impl::remove: {}", r.error());
                return unexpected("unable to update database");
            }
        }
    }
    return *matches;
}

util::expected<std::vector<uenv_record>, std::string>
repository_impl::query(const uenv_label& label) const {
    std::vector<uenv_record> results;

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
    const std::string query =
        query_terms.empty() ? "SELECT * FROM records"
                            : fmt::format("SELECT * FROM records WHERE {}",
                                          fmt::join(query_terms, " AND "));

    auto s = create_query(query, db);
    if (!s) {
        return unexpected(
            fmt::format("creating database query: {}", s.error()));
    }

    auto record_from_query = [](auto& stmnt) -> hopefully<uenv_record> {
        // unsafe: unwrap using .value() without checking for errors.
        // the best way to do this would be to "validate" the database
        // beforehand by checking the columns exist. Even better,
        // validate that column 0 -> 'system', etc, and use integer
        // indexes to look up more effiently.
        auto date =
            parse_uenv_date(static_cast<std::string>(stmnt["date"].value()));
        if (!date) {
            fmt::println("date: {}", date.error().message());
            return unexpected(
                fmt::format("invalid date {}", date.error().message()));
        }
        return uenv_record{
            stmnt["system"].value(),     stmnt["uarch"].value(),
            stmnt["name"].value(),       stmnt["version"].value(),
            stmnt["tag"].value(),        date.value(),
            stmnt["size"].value(),       sha256(stmnt["sha256"].value()),
            uenv_id(stmnt["id"].value())};
    };

    while (s->step()) {
        results.push_back(record_from_query(*s).value());
    }

    // now check for id and sha search terms
    if (label.only_name()) {
        query_terms.erase(query_terms.begin());
        // search for an if name could also be an id
        if (is_sha(*label.name, 16)) {
            query_terms.push_back(
                fmt::format("id = '{}'", uenv_id(*label.name).string()));
            auto result =
                create_query(fmt::format("SELECT * FROM records WHERE {}",
                                         fmt::join(query_terms, " AND ")),
                             db);
            if (result) {
                while (result->step()) {
                    results.push_back(record_from_query(*result).value());
                }
            }
        }
        // search for a sha if name could also be a sha256
        else if (is_sha(*label.name, 64)) {
            query_terms.push_back(
                fmt::format("sha256 = '{}'", sha256(*label.name).string()));
            auto result =
                create_query(fmt::format("SELECT * FROM records WHERE {}",
                                         fmt::join(query_terms, " AND ")),
                             db);
            if (result) {
                while (result->step()) {
                    results.push_back(record_from_query(*result).value());
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
        )",
        R"(CREATE TRIGGER delete_orphan_uenv
            AFTER DELETE ON tags
            FOR EACH ROW
            BEGIN
                DELETE FROM uenv
                WHERE version_id NOT IN (SELECT DISTINCT version_id FROM tags);
            END;
        )",
        R"(CREATE TRIGGER delete_orphan_image
            AFTER DELETE ON tags
            FOR EACH ROW
            BEGIN
                DELETE FROM images
                WHERE sha256 NOT IN (SELECT DISTINCT sha256 FROM tags);
            END;
        )",
    };
}

bool record_set::empty() const {
    return records_.empty();
}
std::size_t record_set::size() const {
    return records_.size();
}

bool record_set::unique_sha() const {
    if (empty()) {
        return false;
    }
    if (size() == 1u) {
        return true;
    }
    auto sha = records_.front().sha;
    for (auto& r : records_) {
        if (r.sha != sha) {
            return false;
        }
    }
    return true;
};

record_set::iterator record_set::begin() {
    return records_.begin();
}
record_set::iterator record_set::end() {
    return records_.end();
}
record_set::const_iterator record_set::begin() const {
    return records_.begin();
}
record_set::const_iterator record_set::end() const {
    return records_.end();
}
record_set::const_iterator record_set::cbegin() const {
    return records_.cbegin();
}
record_set::const_iterator record_set::cend() const {
    return records_.cend();
}

// wrapping the pimpled implementation

repository::~repository() = default;

std::optional<fs::path> repository::path() const {
    return impl_->path;
}

util::expected<record_set, std::string>
repository::query(const uenv_label& label) const {
    return impl_->query(label);
}

util::expected<void, std::string> repository::add(const uenv_record& r) {
    return impl_->add(r);
}

util::expected<record_set, std::string>
repository::remove(const uenv_record& r) {
    return impl_->remove(r);
}

util::expected<record_set, std::string> repository::remove(const sha256& r) {
    return impl_->remove(r);
}

bool repository::is_in_memory() const {
    return impl_->path.has_value();
}

bool repository::is_readonly() const {
    return impl_->is_readonly;
}

repository::pathset repository::uenv_paths(sha256 sha) const {
    return impl_->uenv_paths(sha);
}

bool repository::contains(const uenv_record& r) const {
    return impl_->contains(r);
}

} // namespace uenv
