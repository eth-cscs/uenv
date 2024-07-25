#pragma once

#include <optional>
#include <string>

#include <lib/expected.hpp>

namespace db {

struct uenv_desc {
  using entry_t = std::optional<std::string>;
  entry_t name;
  entry_t version;
  entry_t tag;
  entry_t sha;
};

util::expected<std::string, std::string>
find_image(const uenv_desc &desc, const std::string &repo_path,
           std::optional<std::string> uenv_arch);

} // namespace db
