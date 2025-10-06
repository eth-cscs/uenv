#pragma once

#include <filesystem>

#include <util/envvars.h>
#include <util/expected.h>
#include <util/lustre.h>

namespace lustre {

struct status {
    // number of OSTs to stripe over (-1 -> all)
    std::int64_t count = -1;
    // number of bytes per stripe
    std::uint64_t size = 1024u * 1024u;
    // OST index of first stripe (-1 -> default)
    std::int64_t index = -1;
};

// types of error that can be generated when making lustre calls
enum class error {
    okay,       // what it says on the tin
    not_lustre, // not a lustre file system
    no_lfs,     // the lfs utility was not available on the system
    lfs,        // there was an error calling lfs
    other,      // ...
};

// return true if p is a regular file or directory in a lustre filesystem
// return false under all other circumstances
bool is_lustre(const std::filesystem::path& p);

// get striping information about a file or path
util::expected<status, error> getstripe(const std::filesystem::path& p,
                                        const envvars::state& env);

bool setstripe(const std::filesystem::path& p, const status&,
               const envvars::state& env);

} // namespace lustre
