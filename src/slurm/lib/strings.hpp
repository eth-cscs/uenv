#include <string>
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
std::vector<std::string> split(const std::string &s, const char delim,
                               const bool drop_empty = false);

bool is_sha(const std::string &str);
} // namespace util
