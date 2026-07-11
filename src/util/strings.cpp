#include <algorithm>
#include <cctype>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include "strings.h"

namespace util {

std::string_view trim(std::string_view s) {
    auto is_space = [](char c) {
        return std::isspace(static_cast<unsigned char>(c)) != 0;
    };
    while (!s.empty() && is_space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

std::string strip(std::string_view input) {
    return std::string{trim(input)};
}

std::string to_lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::vector<std::string> split(std::string_view s, const char delim,
                               const bool drop_empty) {
    std::vector<std::string> results;

    auto pos = s.cbegin();
    auto end = s.cend();
    auto next = std::find(pos, end, delim);
    while (next != end) {
        if (!drop_empty || pos != next) {
            results.emplace_back(pos, next);
        }
        pos = next + 1;
        next = std::find(pos, end, delim);
    }
    if (!drop_empty || pos != next) {
        results.emplace_back(pos, next);
    }
    return results;
}

std::string join(std::string_view joiner,
                 const std::vector<std::string>& list) {
    if (list.empty()) {
        return "";
    }
    if (list.size() == 1) {
        return list[0];
    }

    bool first = true;
    std::string result;
    result.reserve(std::accumulate(
        list.begin(), list.end(), (list.size() + 1) * joiner.size(),
        [](std::size_t sum, const std::string& s) { return sum + s.size(); }));

    for (auto& s : list) {
        if (!first) {
            result += joiner;
        }
        result += s;
        first = false;
    }

    return result;
}

std::optional<std::string> base64_decode(std::string_view input) {
    auto value = [](unsigned char c) -> int {
        if (c >= 'A' && c <= 'Z') {
            return c - 'A';
        }
        if (c >= 'a' && c <= 'z') {
            return c - 'a' + 26;
        }
        if (c >= '0' && c <= '9') {
            return c - '0' + 52;
        }
        if (c == '+') {
            return 62;
        }
        if (c == '/') {
            return 63;
        }
        return -1;
    };

    std::string out;
    int buffer = 0;
    int bits = 0;
    for (unsigned char c : input) {
        if (c == '=' || std::isspace(c)) {
            continue; // padding / whitespace: ignore
        }
        int v = value(c);
        if (v < 0) {
            return std::nullopt; // invalid character
        }
        buffer = (buffer << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace util
