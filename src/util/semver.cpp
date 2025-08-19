#include <tuple>

#include <util/semver.h>

namespace util {

bool operator==(const semver& lhs, const semver& rhs) {
    const auto l = std::tie(lhs.major, lhs.minor, lhs.patch);
    const auto r = std::tie(rhs.major, rhs.minor, rhs.patch);
    return l == r && lhs.prerelease == rhs.prerelease;
}

bool operator<(const semver& lhs, const semver& rhs) {
    // compare based only on major.minor.patch
    const auto l = std::tie(lhs.major, lhs.minor, lhs.patch);
    const auto r = std::tie(rhs.major, rhs.minor, rhs.patch);
    if (l < r) {
        return true;
    }
    if (l > r) {
        return false;
    }

    // if there is a tie, check the prerelease
    if (!lhs.prerelease && !rhs.prerelease) {
        // equal
        return false;
    }
    if (lhs.prerelease && !rhs.prerelease) {
        return true;
    }
    if (!lhs.prerelease && rhs.prerelease) {
        return false;
    }
    // compare strings - this ignores the spec because we should break each
    // string into dot-separated terms and compare term by term.
    return lhs.prerelease.value() < rhs.prerelease.value();
}

} // namespace util
