#pragma once

#include <string>

#include <util/expected.h>

namespace uenv {

struct mount_entry {
    std::string sqfs_path;
    std::string mount_path;

    util::expected<void, std::string> validate() const;
};

} // namespace uenv
