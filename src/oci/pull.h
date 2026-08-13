#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include <oci/client.h>
#include <oci/digest.h>
#include <oci/manifest.h>
#include <util/expected.h>

namespace oci {

// progress callback for the squashfs download: (bytes_downloaded, bytes_total).
using progress_fn = std::function<void(std::uint64_t, std::uint64_t)>;

// Download the squashfs layer of `image` into <store>/store.squashfs and
// self-verify: the file is re-digested and checked against the layer digest, so
// a truncated or corrupt download is caught locally. `image` is the parsed
// image manifest. An optional abort predicate, polled during the download,
// cancels it (for Ctrl-C handling).
util::expected<void, std::string>
pull_squashfs(client& c, const manifest& image,
              const std::filesystem::path& store, progress_fn progress = {},
              std::function<bool()> should_abort = {});

// Download and unpack the `uenv/meta` referrer (a gzipped tar) into <store>,
// reproducing the `meta/` directory. Returns true if meta was found and
// pulled, false if the image has no attached meta.
util::expected<bool, std::string> pull_meta(client& c,
                                            const digest& manifest_digest,
                                            const std::filesystem::path& store);

} // namespace oci
