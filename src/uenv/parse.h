#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <util/expected.h>

#include <uenv/lex.h>
#include <uenv/mount.h>
#include <uenv/uenv.h>
#include <uenv/view.h>

namespace uenv {

/// represents an error generated when parsing a string.
///
/// stores the input string and the location (loc) in the string where the error
/// was encountered
///
/// there are two levels of error message:
/// - detail: a detailed low level description (e.g. "unexpected symbol ?") that
/// correlates to the loc in input
/// - description: a high level description, usually added at a higher level
/// (e.g. "invalid --uenv argument")
struct parse_error {
    std::string input;
    std::string description;
    std::string detail;
    unsigned loc;
    unsigned width;
    parse_error(std::string input, std::string detail, const token& tok)
        : input(std::move(input)), detail(std::move(detail)), loc(tok.loc),
          width(tok.spelling.length()) {
    }
    parse_error(std::string input, std::string description, std::string detail,
                const token& tok)
        : input(std::move(input)), description(std::move(description)),
          detail(std::move(detail)), loc(tok.loc),
          width(tok.spelling.length()) {
    }
    std::string message() const;
};

// apply to strings before parsing them.
// - removes tab, vertical tab, newline and carriage return.
std::string sanitise_input(std::string_view input);

util::expected<std::vector<view_description>, parse_error>
parse_view_args(const std::string& arg);

util::expected<std::vector<uenv_description>, parse_error>
parse_uenv_args(const std::string& arg);

util::expected<std::vector<mount_entry>, parse_error>
parse_mount_list(const std::string& arg);

util::expected<std::string, parse_error> parse_path(const std::string& in);

} // namespace uenv
