#pragma once

#include <uenv/repository.h>

namespace uenv {
void print_record_set(const record_set& result, bool no_header);
std::string format_record_set(const record_set& records);
} // namespace uenv
