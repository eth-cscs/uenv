#pragma once

#include <exception>
#include <string>

struct sqlite3_stmt;
struct sqlite3;

enum class sqlite_open : int { readonly };

class SQLiteError : public std::exception {
  public:
    SQLiteError(const std::string& msg) : msg(msg) {
    }
    const char* what() const noexcept override {
        return msg.c_str();
    }

  private:
    std::string msg;
};

class SQLiteStatement;

class SQLiteDB {
  public:
    SQLiteDB(const std::string& fname, sqlite_open flag);
    SQLiteDB(const SQLiteDB&) = delete;
    SQLiteDB operator=(const SQLiteDB&) = delete;
    virtual ~SQLiteDB();

  private:
    sqlite3* db{nullptr};

  protected:
    sqlite3* get() {
        return db;
    }

    friend SQLiteStatement;
};

class SQLiteColumn;

class SQLiteStatement {
  public:
    SQLiteStatement(SQLiteDB& db, const std::string& query);
    SQLiteStatement(const SQLiteStatement&) = delete;
    SQLiteStatement operator=(const SQLiteStatement&) = delete;
    std::string getColumnType(int i) const;
    SQLiteColumn getColumn(int i) const;
    int getColumnIndex(const std::string& name) const;
    void bind(const std::string& name, const std::string& value);
    bool execute();

    virtual ~SQLiteStatement();

  private:
    void checkIndex(int i) const;

  private:
    SQLiteDB& db;
    sqlite3_stmt* stmt;
    int column_count{-1};
    int rc;
    friend SQLiteColumn;
};

class SQLiteColumn {
  public:
    SQLiteColumn(const SQLiteStatement& statement, int index);
    std::string name() const;
    operator int() const;
    operator std::string() const;

  private:
    const SQLiteStatement& statement;
    const int index;
};
