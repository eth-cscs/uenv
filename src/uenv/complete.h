#pragma once

// Candidates for the tab completion of the values of uenv arguments: labels,
// uenv descriptions, views, systems, repositories and paths.
//
// These are pure functions of the text typed so far and of data that the
// caller has already loaded (repository records, image meta data), apart from
// the paths, which are listed from the file system. The value of each
// candidate replaces the whole of `prefix`.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <argparse/complete.h>
#include <uenv/meta.h>
#include <uenv/repository.h>
#include <uenv/uenv.h>
#include <util/envvars.h>

namespace uenv {

using argparse::candidate;

// Which paths to offer. Directories are always offered, so that the user can
// descend into them; their values end in '/'.
struct path_filter {
    // offer files, not only directories
    bool files = true;
    // if set, only offer files whose name matches this glob
    std::optional<std::string> glob;
};

// Paths that start with `prefix`, relative to the current working directory
// unless absolute. A leading `~/` and variables ($HOME/) in the directory are
// expanded using `env` to list it, and kept in the values. Hidden files are
// offered only if the last component of `prefix` starts with '.'.
std::vector<candidate> complete_path(std::string_view prefix,
                                     const path_filter& filter,
                                     const envvars::state& env);

// The labels of the records, as name/version:tag. Only the records on
// `system` (if set) are offered, unless the user has typed '@' to give the
// system, in which case the labels of every system are offered with it.
// Once '%' has been typed the uarch is added.
std::vector<candidate> complete_label(std::string_view prefix,
                                      const std::vector<uenv_record>& records,
                                      const std::optional<std::string>& system);

// A uenv description: a label, or a path to a squashfs file, either of which
// can be followed by :mount-point. Squashfs files are offered if the prefix
// looks like a path (a relative path must start with ./ to be read as one),
// and in the current directory if nothing has been typed and there are no
// labels to offer.
std::vector<candidate> complete_uenv(std::string_view prefix,
                                     const std::vector<uenv_record>& records,
                                     const std::optional<std::string>& system,
                                     const envvars::state& env);

// A comma separated list of uenv descriptions (uenv start, uenv run).
std::vector<candidate> complete_uenv_list(
    std::string_view prefix, const std::vector<uenv_record>& records,
    const std::optional<std::string>& system, const envvars::state& env);

// A comma separated list of views (--view), of the uenvs named on the command
// line: `uenvs` pairs the name of each uenv with its meta data. Views are
// given as view, or as uenv:view when more than one uenv is named or once ':'
// has been typed.
std::vector<candidate>
complete_views(std::string_view prefix,
               const std::vector<std::pair<std::string, meta>>& uenvs);

// The systems that the records are for.
std::vector<candidate> complete_system(std::string_view prefix,
                                       const std::vector<uenv_record>& records);

// A comma separated list of repositories (--repo): each is the name of a
// configured repository, a path, or name=path.
std::vector<candidate> complete_repo(std::string_view prefix,
                                     const repo_list& repos,
                                     const envvars::state& env);

// A word as the shell would pass it to a command: quotes and backslash
// escapes are removed. The word can be incomplete (e.g. an open quote), and
// no expansion is performed.
std::string shell_unquote(std::string_view word);

// A complete word as the shell would pass it to a command: unquoted, with a
// leading ~ (on its own, or followed by /) replaced by HOME, and variables
// ($NAME, ${NAME}) outside single quotes replaced by their values in `env`.
// Completion is given the words as they were typed, before expansion.
std::string shell_expand(std::string_view word, const envvars::state& env);

} // namespace uenv
