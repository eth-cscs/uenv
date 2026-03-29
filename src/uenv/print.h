#pragma once

#include <string_view>
#include <vector>

#include <uenv/repository.h>
#include <util/expected.h>

namespace uenv {

enum class record_set_format { table, table_no_header, json, list };

// Pairs a repo_description with the records found in that repo.
struct repo_record_set {
    repo_description repo;
    record_set records;
};

// Print results from one or more repos.
// Table: one section per repo, each with a "repo: name (path)" header,
//   followed by a column-header row and matching records.
//   With --no-header, all records are printed flat with no separators.
// JSON: flat {"records": [...]} envelope; each record carries a
//   "repo": {"name": ..., "path": ...} field identifying its source.
// List/format: {repo} and {repo_path} are available as named substitution args.
void print_repo_record_sets(
    const std::vector<repo_record_set>& results, record_set_format format,
    std::string_view fmtstring = "{name}/{version}:{tag}@{system}%{uarch}");

// Single-repo variants (used by image find and other callers).
void print_record_set(
    const record_set& result, record_set_format format,
    std::string_view fmtstring = "{name}/{version}:{tag}@{system}%{uarch}");

std::string format_record_set_table(const record_set& records,
                                    bool no_header = true);
std::string format_record_set_json(const record_set& records);
std::string format_record_set_format(const record_set& records,
                                     std::string_view fmtstring);

// a helper for determining the output format based on CLI flags:
// --no-header, --json and --format
util::expected<record_set_format, std::string>
get_record_set_format(bool no_header, bool json, bool list);

} // namespace uenv
