#pragma once

#include <uenv/repository.h>
#include <util/expected.h>

namespace uenv {

enum class record_set_format { table, table_no_header, json, list };

void print_record_set(const record_set& result, record_set_format format);

std::string format_record_set_table(const record_set& records,
                                    bool no_header = true);
std::string format_record_set_list(const record_set& records);
std::string format_record_set_json(const record_set& records);

// a helper for determining the output format based on CLI flags:
// --no-header, --json and --list
util::expected<record_set_format, std::string>
get_record_set_format(bool no_header, bool json, bool list);

} // namespace uenv
