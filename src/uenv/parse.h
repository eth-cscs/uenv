#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <util/expected.h>

#include <uenv/uenv.h>
#include <uenv/view.h>

namespace uenv {

struct parse_error {
    std::string msg;
    unsigned loc;
    parse_error(std::string msg, unsigned loc) : msg(std::move(msg)), loc(loc) {
    }
};

// apply to strings before parsing them.
// - removes tab, vertical tab, newline and carriage return.
std::string sanitise_input(std::string_view input);

util::expected<std::vector<view_description>, parse_error>
parse_view_args(const std::string& arg);

util::expected<std::vector<uenv_description>, parse_error>
parse_uenv_args(const std::string& arg);

} // namespace uenv
