#include <string_view>

#include <util/sha.h>

namespace util {

bool is_sha_string(std::string_view s) {
    for (char c : s) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) {
            return false;
        }
    }
    return true;
}

} // namespace util
