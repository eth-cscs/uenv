#pragma once

#include <vector>

#include <uenv/uenv.h>

namespace uenv {
void print_record_list(const std::vector<uenv::uenv_record>& result,
                       bool no_header);
}
