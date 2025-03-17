#include <numeric>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "strings.h"

namespace util {

std::string strip(std::string_view input) {
    if (input.empty()) {
        return {};
    }
    auto b = std::find_if_not(input.begin(), input.end(), std::iswspace);
    if (b == input.end()) {
        return {};
    }
    auto e = input.end();
    while (std::iswspace(*--e))
        ;
    return {b, e + 1};
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

bool is_full_sha256(const std::string& str) {
    std::regex pattern("^[a-fA-F0-9]{64}$");
    std::smatch match;
    std::regex_match(str, match, pattern);
    if (!match.empty()) {
        return true;
    }
    return false;
}

bool is_id(const std::string& str) {
    std::regex pattern("^[a-fA-F0-9]{16}$");
    std::smatch match;
    std::regex_match(str, match, pattern);
    if (!match.empty()) {
        return true;
    }
    return false;
}

bool is_sha(const std::string& str) {
    if (is_id(str) || is_full_sha256(str)) {
        return true;
    }
    return false;
}

} // namespace util
