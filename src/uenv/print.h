#pragma once

#include <uenv/repository.h>

namespace uenv {
void print_record_set(const record_set& result, bool no_header = true,
                      bool json = false);
std::string format_record_set(const record_set& records, bool no_header = true);
std::string format_record_set_json(const record_set& records);
} // namespace uenv
