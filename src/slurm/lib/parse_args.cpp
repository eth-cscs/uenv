#include <algorithm>
#include <optional>
#include <regex>
#include <set>

#include "config.hpp"
#include "parse_args.hpp"
#include <lib/database.hpp>
#include <lib/expected.hpp>
#include <lib/filesystem.hpp>
#include <lib/strings.hpp>

// abs path
#define LINUX_ABS_FPATH "/[^\\0,:]+"
#define JFROG_IMAGE "[^\\0,:/]+"

namespace util {

const std::regex default_pattern("^(?:(file:\\/\\/))?"
                                 "(" LINUX_ABS_FPATH ")"
                                 "(:" LINUX_ABS_FPATH ")?",
                                 std::regex::ECMAScript);
// match <image-name>/?<version>:?<tag>:?<abs-mount-path>
// deviates from the official scheme:
// https://github.com/opencontainers/distribution-spec/blob/main/spec.md#pulling-manifests
const std::regex repo_pattern("(" JFROG_IMAGE ")"
                              "(/[a-zA-Z0-9._-]+)?"
                              "(:[a-zA-Z0-9._-]+)?"
                              "(:" LINUX_ABS_FPATH ")?",
                              std::regex::ECMAScript);

/*
 return dictionary{"name", "version", "tag", "sha" } from a uenv description
 string

 prgenv_gnu              ->("prgenv_gnu", None, None, None)
 prgenv_gnu/23.11        ->("prgenv_gnu", "23.11", None, None)
 prgenv_gnu/23.11:latest ->("prgenv_gnu", "23.11", "latest", None)
 prgenv_gnu:v2           ->("prgenv_gnu", None, "v2", None)
 3313739553fe6553        ->(None, None, None, "3313739553fe6553")
 */
db::uenv_desc parse_uenv_string(const std::string &entry) {
  db::uenv_desc res;

  if (util::is_sha(entry)) {
    res.sha = entry;
    return res;
  }

  std::smatch match;
  std::regex_match(entry, match, repo_pattern);

  if (const auto name = match[1]; name.matched) {
    res.name = name.str();
  }

  if (const auto version = match[2]; version.matched) {
    // remove the leading '/'
    res.version = version.str().erase(0, 1);
  }

  if (const auto tag = match[3]; tag.matched) {
    // remove the leading ':'
    res.tag = tag.str().erase(0, 1);
  }

  return res;
}

util::expected<std::vector<util::mount_entry>, std::string>
parse_arg(const std::string &arg, std::optional<std::string> uenv_repo_path,
          std::optional<std::string> uenv_arch) {
  std::vector<std::string> arguments = util::split(arg, ',', true);

  if (arguments.empty()) {
    return {};
  }

  auto get_mount_point = [](std::ssub_match sub_match) -> std::string {
    if (sub_match.matched) {
      return std::string(sub_match).erase(0, 1);
    }
    return std::string{DEFAULT_MOUNT_POINT};
  };

  std::vector<util::mount_entry> mount_entries;
  for (auto &entry : arguments) {
    if (std::smatch match; std::regex_match(entry, match, default_pattern)) {
      std::string image_path = match[2];
      std::string mount_point = get_mount_point(match[3]);
      mount_entries.emplace_back(util::mount_entry{image_path, mount_point});
    } else if (std::smatch match;
               std::regex_match(entry, match, repo_pattern)) {
      auto desc = parse_uenv_string(entry);
      if (!uenv_repo_path) {
        return util::unexpected("Attempting to open from uenv repository. But "
                                "either $" UENV_REPO_PATH_VARNAME
                                " or $SCRATCH is not set.");
      }
      auto image_path = db::find_image(desc, uenv_repo_path.value(), uenv_arch);
      if (!image_path.has_value()) {
        return util::unexpected(image_path.error());
      }
      mount_entries.emplace_back(
          util::mount_entry{image_path.value(), get_mount_point(match[4])});
    } else {
      // no match found
      return util::unexpected(
          "Invalid syntax for --uenv, expected format is: "
          "\"<image>[:mount-point][,<image:mount-point>]*\""
          "\n where <image> is either an absolute path or an image. Run `uenv "
          "image ls` to see a list of available images.\n"
          "mount-point must be an absolute path.");
    }
  }

  // check for relative paths
  for (const auto &entry : mount_entries) {
    bool is_abs_path =
        entry.image_path[0] == '/' && entry.mount_point[0] == '/';
    if (!is_abs_path)
      return util::unexpected("Absolute path expected in " + entry.image_path +
                              ":" + entry.mount_point);
  }
  // sort by mountpoint
  std::sort(mount_entries.begin(), mount_entries.end(),
            [](const util::mount_entry &a, const util::mount_entry &b) {
              return a.mount_point < b.mount_point;
            });

  // check for duplicates
  std::set<std::string> set_mnt_points;
  std::for_each(mount_entries.begin(), mount_entries.end(),
                [&set_mnt_points](const auto &e) {
                  set_mnt_points.insert(e.mount_point);
                });
  if (set_mnt_points.size() != mount_entries.size()) {
    return util::unexpected("Duplicate mountpoints found.");
  }
  std::set<std::string> set_images;
  std::for_each(
      mount_entries.begin(), mount_entries.end(),
      [&set_images](const auto &e) { set_images.insert(e.image_path); });
  if (set_images.size() != mount_entries.size()) {
    return util::unexpected("Duplicate images found.");
  }

  return mount_entries;
}

} // namespace util
