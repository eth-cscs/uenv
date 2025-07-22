#pragma once

#include <string>
#include <vector>

#include <util/expected.h>

namespace uenv {

struct mount_entry {
    std::string sqfs_path;
    std::string mount_path;

    util::expected<void, std::string> validate() const;
};

util::expected<void, std::string>
do_mount(const std::vector<mount_entry>& mount_entries);

} // namespace uenv
