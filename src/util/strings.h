#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace util {

// split a string on a character delimiter
//
// if drop_empty==false (default)
//
// ""       -> [""]
// ","      -> ["", ""]
// ",,"     -> ["", "", ""]
// ",a"     -> ["", "a"]
// "a,"     -> ["a", ""]
// "a"      -> ["a"]
// "a,b"    -> ["a", "b"]
// "a,b,c"  -> ["a", "b", "c"]
// "a,b,,c" -> ["a", "b", "", "c"]
//
// if drop_empty==true
//
// ""       -> []
// ","      -> []
// ",,"     -> []
// ",a"     -> ["a"]
// "a,"     -> ["a"]
// "a"      -> ["a"]
// "a,b"    -> ["a", "b"]
// "a,b,c"  -> ["a", "b", "c"]
// "a,b,,c" -> ["a", "b", "c"]
std::vector<std::string> split(std::string_view s, const char delim,
                               const bool drop_empty = false);

// remove leading and trailing whitespace, returning a view into `input` (no
// allocation). The returned view is only valid while `input` lives.
std::string_view trim(std::string_view input);

// strip whitespace from beginning and end of a string (the owning-string form
// of trim()).
std::string strip(std::string_view input);

// lower-case each (ASCII) character.
std::string to_lower(std::string_view input);

std::string join(std::string_view joiner, const std::vector<std::string>& list);

// decode a standard base64 string (RFC 4648). Padding '=' and embedded
// whitespace are ignored. Returns nullopt if the input contains a character
// outside the base64 alphabet. Note: this is NOT the URL-safe ('-'/'_')
// alphabet.
std::optional<std::string> base64_decode(std::string_view input);
} // namespace util
