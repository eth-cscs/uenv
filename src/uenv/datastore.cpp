#include <any>
#include <cstdint>
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

struct datastore_impl {
    datastore_impl(sqlite_database db, fs::path path, fs::path db_path)
        : db(std::move(db)), path(std::move(path)),
          db_path(std::move(db_path)) {
    }
    datastore_impl(datastore_impl&&) = default;
    sqlite_database db;
    fs::path path;
    fs::path db_path;

    util::expected<std::vector<uenv_record>, std::string>
    query(const uenv_label& label);
};

datastore::datastore(datastore&&) = default;
datastore::datastore(std::unique_ptr<datastore_impl> impl)
    : impl_(std::move(impl)) {
}

util::expected<datastore, std::string>
open_datastore(const fs::path& repo_path) {
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

    return datastore(
        std::make_unique<datastore_impl>(std::move(*db), repo_path, db_path));
}

util::expected<std::vector<uenv_record>, std::string>
datastore_impl::query(const uenv_label& label) {
    std::vector<uenv_record> results;

    std::string query = fmt::format("SELECT * FROM records");
    // fmt::format("SELECT * FROM records WHERE name = '{}'", label.name);
    if (label.tag) {
        query += fmt::format(" AND tag = '{}'", *label.tag);
    }
    if (label.version) {
        query += fmt::format(" AND version = '{}'", *label.version);
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

datastore::~datastore() = default;

const fs::path& datastore::path() const {
    return impl_->path;
}

const fs::path& datastore::db_path() const {
    return impl_->db_path;
}

util::expected<std::vector<uenv_record>, std::string>
datastore::query(const uenv_label& label) {
    return impl_->query(label);
}

} // namespace uenv
