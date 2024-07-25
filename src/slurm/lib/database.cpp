#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "database.hpp"
#include "filesystem.hpp"
#include "sqlite.hpp"

namespace db {

struct uenv_result {
  std::string name;
  std::string version;
  std::string tag;
  std::string sha;

  uenv_result() = delete;

  uenv_result(std::string name, std::string version, std::string tag,
              std::string sha)
      : name(name), version(version), tag(tag), sha(sha) {}
  uenv_result(SQLiteStatement &result)
      : uenv_result(result.getColumn(result.getColumnIndex("name")),
                    result.getColumn(result.getColumnIndex("version")),
                    result.getColumn(result.getColumnIndex("tag")),
                    result.getColumn(result.getColumnIndex("sha256"))) {}
};

util::expected<std::string, std::string>
find_image(const uenv_desc &desc, const std::string &repo_path,
           std::optional<std::string> uenv_arch) {
  try {
    std::string dbpath = repo_path + "/index.db";
    // check if dbpath exists.
    if (!util::is_file(dbpath)) {
      return util::unexpected("Can't open uenv repo. " + dbpath +
                              " is not a file.");
    }
    SQLiteDB db(dbpath, sqlite_open::readonly);

    // get all results
    std::vector<uenv_result> results;
    if (desc.sha) {
      if (desc.sha.value().size() < 64) {
        SQLiteStatement query(db, "SELECT * FROM records WHERE id = :id");
        query.bind(":id", desc.sha.value());
        while (query.execute()) {
          results.emplace_back(query);
        }
      } else {
        SQLiteStatement query(db, "SELECT * FROM records WHERE sha256 = :sha");
        query.bind(":sha", desc.sha.value());
        while (query.execute()) {
          results.emplace_back(query);
        }
      }
    } else {
      std::string query_str = "SELECT * FROM records WHERE ";
      std::vector<std::string> filter;
      if (uenv_arch) {
        filter.push_back("uarch");
      }
      if (desc.name) {
        filter.push_back("name");
      }
      if (desc.version) {
        filter.push_back("version");
      }
      if (desc.tag) {
        filter.push_back("tag");
      }
      for (size_t i = 0; i < filter.size(); ++i) {
        if (i > 0) {
          query_str += " AND ";
        }
        query_str += filter[i] + " = " + ":" + filter[i];
      }
      SQLiteStatement query(db, query_str);
      if (uenv_arch) {
        query.bind(":uarch", uenv_arch.value());
      }
      if (desc.name) {
        query.bind(":name", desc.name.value());
      }
      if (desc.version) {
        query.bind(":version", desc.version.value());
      }
      if (desc.tag) {
        query.bind(":tag", desc.tag.value());
      }
      while (query.execute()) {
        results.emplace_back(query);
      }
    }

    // sort the results by sha, and build a list of unique sha
    std::sort(results.begin(), results.end(),
              [](auto &lhs, auto &rhs) { return lhs.sha < rhs.sha; });
    std::set<std::string> shas;
    for (const auto &r : results) {
      shas.insert(r.sha);
    }
    if (shas.size() > 1) {
      std::stringstream ss;
      ss << "More than one uenv matches.\n";
      for (auto &d : results) {
        ss << d.name << "/" << d.version << ":" << d.tag << "\t" << d.sha
           << "\n";
      }
      return util::unexpected(ss.str());
    }
    if (results.empty()) {
      return util::unexpected("No uenv matches the request. Run `uenv image "
                              "ls` to list available images.");
    }
    return repo_path + "/images/" + results[0].sha + "/store.squashfs";
  } catch (const SQLiteError &e) {
    return util::unexpected(std::string("internal database error: ") +
                            e.what());
  }
}

} // namespace db
