#include <regex>
#include <string>
#include <vector>

#include "strings.hpp"

namespace util {

std::vector<std::string> split(const std::string &s, const char delim,
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

bool is_full_sha256(const std::string &str) {
  std::regex pattern("^[a-fA-F0-9]{64}$");
  std::smatch match;
  std::regex_match(str, match, pattern);
  if (!match.empty()) {
    return true;
  }
  return false;
}

bool is_id(const std::string &str) {
  std::regex pattern("^[a-fA-F0-9]{16}$");
  std::smatch match;
  std::regex_match(str, match, pattern);
  if (!match.empty()) {
    return true;
  }
  return false;
}

bool is_sha(const std::string &str) {
  if (is_id(str) || is_full_sha256(str)) {
    return true;
  }
  return false;
}

} // namespace util
