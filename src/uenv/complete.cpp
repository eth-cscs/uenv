#include <fnmatch.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fmt/format.h>

#include <uenv/complete.h>

namespace uenv {

namespace {

// prepend `head` to the value of every candidate
std::vector<candidate> prefixed(std::string_view head,
                                std::vector<candidate> candidates) {
    for (auto& c : candidates) {
        c.value.insert(0, head);
    }
    return candidates;
}

void sort_unique(std::vector<candidate>& c) {
    std::stable_sort(c.begin(), c.end(), [](const auto& a, const auto& b) {
        return a.value < b.value;
    });
    auto last =
        std::unique(c.begin(), c.end(), [](const auto& a, const auto& b) {
            return a.value == b.value;
        });
    c.erase(last, c.end());
}

// the text up to and including the last `sep`, and the text after it
std::pair<std::string_view, std::string_view> split_last(std::string_view s,
                                                         char sep) {
    auto pos = s.rfind(sep);
    if (pos == std::string_view::npos) {
        return {{}, s};
    }
    return {s.substr(0, pos + 1), s.substr(pos + 1)};
}

// a uenv description that starts like this is a path, not a label (see
// parse_uenv_description)
bool is_path_like(std::string_view s) {
    return s.starts_with('/') || s.starts_with('.') || s.starts_with('~') ||
           s.starts_with('$');
}

bool is_name_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
bool is_name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// If word[i] starts a variable reference, $NAME or ${NAME}, append its value
// (empty if it is not set) to `out` and return the number of characters it
// takes; otherwise return 0.
std::size_t variable(std::string_view word, std::size_t i,
                     const envvars::state& env, std::string& out) {
    std::string_view name;
    std::size_t length = 0;
    if (word.substr(i).starts_with("${")) {
        auto close = word.find('}', i + 2);
        if (close == std::string_view::npos) {
            return 0;
        }
        name = word.substr(i + 2, close - i - 2);
        length = close - i + 1;
        if (!std::all_of(name.begin(), name.end(), is_name_char)) {
            return 0;
        }
    } else {
        auto j = i + 1;
        while (j < word.size() && is_name_char(word[j])) {
            ++j;
        }
        name = word.substr(i + 1, j - i - 1);
        length = j - i;
    }
    if (name.empty() || !is_name_start(name.front())) {
        return 0;
    }
    out += env.get(name).value_or("");
    return length;
}

// If `word` starts with a reference to the home directory, ~ or ~/, and HOME
// is set, append its value to `out` and return the number of characters the
// reference takes, which is 1; otherwise return 0.
std::size_t home(std::string_view word, const envvars::state& env,
                 std::string& out) {
    if (word == "~" || word.starts_with("~/")) {
        if (auto value = env.get("HOME")) {
            out += *value;
            return 1;
        }
    }
    return 0;
}

// the path named by text typed by the user: a leading ~ and variables are
// expanded, as the shell will when the command is run
std::string expand_path(std::string_view text, const envvars::state& env) {
    std::string result;
    std::size_t i = home(text, env, result);
    while (i < text.size()) {
        if (text[i] == '$') {
            if (auto n = variable(text, i, env, result)) {
                i += n;
                continue;
            }
        }
        result += text[i++];
    }
    return result;
}

// shell_unquote, and if env is set, shell_expand
std::string unquote(std::string_view word, const envvars::state* env) {
    std::string result;
    std::size_t i = env ? home(word, *env, result) : 0;
    enum { none, single, dbl } quote = none;
    for (; i < word.size(); ++i) {
        const char c = word[i];
        if (c == '$' && env && quote != single) {
            if (auto n = variable(word, i, *env, result)) {
                i += n - 1;
                continue;
            }
        }
        switch (quote) {
        case single:
            if (c == '\'') {
                quote = none;
            } else {
                result += c;
            }
            break;
        case dbl:
            if (c == '"') {
                quote = none;
            } else if (c == '\\' && i + 1 < word.size() &&
                       std::string_view("\"\\$`").find(word[i + 1]) !=
                           std::string_view::npos) {
                result += word[++i];
            } else {
                result += c;
            }
            break;
        case none:
            if (c == '\'') {
                quote = single;
            } else if (c == '"') {
                quote = dbl;
            } else if (c == '\\' && i + 1 < word.size()) {
                result += word[++i];
            } else if (c != '\\') {
                result += c;
            }
            break;
        }
    }
    return result;
}

const path_filter squashfs_files{.files = true, .glob = "*.squashfs"};
const path_filter directories{.files = false};

} // namespace

std::vector<candidate> complete_path(std::string_view prefix,
                                     const path_filter& filter,
                                     const envvars::state& env) {
    namespace fs = std::filesystem;

    if (prefix == "~") {
        return {{"~/", {}}};
    }
    // the directory is listed with ~ and variables expanded, and the values
    // keep them as they were typed
    auto [dir_text, base] = split_last(prefix, '/');
    std::string dir = expand_path(dir_text, env);
    if (dir.starts_with('~')) {
        // HOME is not set
        return {};
    }
    if (dir.empty()) {
        dir = dir_text.empty() ? '.' : '/';
    }

    std::vector<candidate> result;
    // the parent directory is not listed by the directory iterator
    if (base == "." || base == "..") {
        result.push_back({fmt::format("{}../", dir_text), {}});
    }
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end;
         it.increment(ec)) {
        auto name = it->path().filename().string();
        if (!name.starts_with(base)) {
            continue;
        }
        if (name.starts_with('.') && !base.starts_with('.')) {
            continue;
        }
        std::error_code type_ec;
        if (fs::is_directory(it->path(), type_ec)) {
            result.push_back({fmt::format("{}{}/", dir_text, name), {}});
        } else if (filter.files &&
                   (!filter.glob ||
                    fnmatch(filter.glob->c_str(), name.c_str(), 0) == 0)) {
            result.push_back({fmt::format("{}{}", dir_text, name), {}});
        }
    }
    sort_unique(result);
    return result;
}

std::vector<candidate>
complete_label(std::string_view prefix, const std::vector<uenv_record>& records,
               const std::optional<std::string>& system) {
    const auto at = prefix.find('@');
    const auto pct = prefix.find('%');
    const bool has_system = at != std::string_view::npos;
    const bool has_uarch = pct != std::string_view::npos;

    std::vector<candidate> result;
    for (auto& r : records) {
        if (!has_system && system && r.system != *system) {
            continue;
        }
        auto value = fmt::format("{}/{}:{}", r.name, r.version, r.tag);
        if (has_system && has_uarch) {
            value += at < pct ? fmt::format("@{}%{}", r.system, r.uarch)
                              : fmt::format("%{}@{}", r.uarch, r.system);
        } else if (has_system) {
            value += fmt::format("@{}", r.system);
        } else if (has_uarch) {
            value += fmt::format("%{}", r.uarch);
        }
        if (value.starts_with(prefix)) {
            result.push_back(
                {std::move(value), fmt::format("@{}%{}", r.system, r.uarch)});
        }
    }
    sort_unique(result);
    return result;
}

std::vector<candidate> complete_uenv(std::string_view prefix,
                                     const std::vector<uenv_record>& records,
                                     const std::optional<std::string>& system,
                                     const envvars::state& env) {
    // the mount point follows a ':' after a path, and a ':' that is followed
    // by the start of a path, or a second ':', after a label
    std::size_t mount = std::string_view::npos;
    if (is_path_like(prefix)) {
        mount = prefix.find(':');
    } else if (auto tag = prefix.find(':'); tag != std::string_view::npos) {
        auto after = prefix.substr(tag + 1);
        if (after.starts_with('/') || after.starts_with('.')) {
            mount = tag;
        } else if (auto second = after.find(':');
                   second != std::string_view::npos) {
            mount = tag + 1 + second;
        }
    }
    if (mount != std::string_view::npos) {
        return prefixed(
            prefix.substr(0, mount + 1),
            complete_path(prefix.substr(mount + 1), directories, env));
    }

    if (is_path_like(prefix)) {
        return complete_path(prefix, squashfs_files, env);
    }
    auto labels = complete_label(prefix, records, system);
    if (labels.empty() && prefix.empty()) {
        // there are no labels to offer: offer the files in the current
        // directory, which are only paths when they start with ./
        return complete_path("./", squashfs_files, env);
    }
    return labels;
}

std::vector<candidate> complete_uenv_list(
    std::string_view prefix, const std::vector<uenv_record>& records,
    const std::optional<std::string>& system, const envvars::state& env) {
    auto [head, last] = split_last(prefix, ',');
    return prefixed(head, complete_uenv(last, records, system, env));
}

std::vector<candidate>
complete_views(std::string_view prefix,
               const std::vector<std::pair<std::string, meta>>& uenvs) {
    auto [head, last] = split_last(prefix, ',');
    const bool qualify =
        uenvs.size() > 1 || last.find(':') != std::string_view::npos;

    std::vector<candidate> result;
    for (auto& [name, m] : uenvs) {
        for (auto& [view_name, view] : m.views) {
            auto value =
                qualify ? fmt::format("{}:{}", name, view_name) : view_name;
            if (value.starts_with(last)) {
                result.push_back({std::move(value), view.description});
            }
        }
    }
    sort_unique(result);
    return prefixed(head, std::move(result));
}

std::vector<candidate>
complete_namespace(std::string_view prefix,
                   const std::vector<std::string>& names) {
    std::vector<candidate> result;
    for (auto& name : names) {
        auto value = fmt::format("{}::", name);
        if (value.starts_with(prefix)) {
            result.push_back({std::move(value), "namespace"});
        }
    }
    sort_unique(result);
    return result;
}

std::vector<candidate>
complete_registry_label(std::string_view prefix,
                        const std::vector<std::string>& namespaces,
                        const std::optional<std::string>& default_namespace,
                        const namespace_records& records,
                        const std::optional<std::string>& system) {
    if (auto sep = prefix.find("::"); sep != std::string_view::npos) {
        return prefixed(
            prefix.substr(0, sep + 2),
            complete_label(prefix.substr(sep + 2),
                           records(std::string(prefix.substr(0, sep))),
                           system));
    }
    std::vector<candidate> result;
    if (default_namespace) {
        result = complete_label(prefix, records(*default_namespace), system);
    }
    if (!default_namespace || !prefix.empty()) {
        auto names = complete_namespace(prefix, namespaces);
        result.insert(result.end(), names.begin(), names.end());
    }
    sort_unique(result);
    return result;
}

std::vector<candidate>
complete_registry_destination(std::string_view prefix,
                              const std::vector<std::string>& namespaces,
                              const std::vector<uenv_record>& sources) {
    const auto sep = prefix.find("::");
    if (sep == std::string_view::npos) {
        return complete_namespace(prefix, namespaces);
    }
    const auto rest = prefix.substr(sep + 2);
    std::vector<candidate> result;
    for (auto& r : sources) {
        auto stem = fmt::format("{}/{}:", r.name, r.version);
        const auto at = rest.find('@', stem.size());
        if (stem.starts_with(rest)) {
            result.push_back({std::move(stem), {}});
        } else if (rest.starts_with(stem) && at != std::string_view::npos) {
            auto value =
                fmt::format("{}@{}%{}", rest.substr(0, at), r.system, r.uarch);
            if (value.starts_with(rest)) {
                result.push_back({std::move(value), {}});
            }
        }
    }
    sort_unique(result);
    return prefixed(prefix.substr(0, sep + 2), std::move(result));
}

std::vector<candidate>
complete_system(std::string_view prefix,
                const std::vector<uenv_record>& records) {
    std::vector<candidate> result;
    for (auto& r : records) {
        if (r.system.starts_with(prefix)) {
            result.push_back({r.system, {}});
        }
    }
    sort_unique(result);
    return result;
}

std::vector<candidate> complete_repo(std::string_view prefix,
                                     const repo_list& repos,
                                     const envvars::state& env) {
    auto [head, last] = split_last(prefix, ',');
    std::vector<candidate> result;
    if (auto eq = last.find('='); eq != std::string_view::npos) {
        result = prefixed(last.substr(0, eq + 1),
                          complete_path(last.substr(eq + 1), directories, env));
    } else if (is_path_like(last)) {
        result = complete_path(last, directories, env);
    } else {
        for (auto& r : repos) {
            if (r.name.starts_with(last)) {
                result.push_back({r.name, r.path.string()});
            }
        }
        sort_unique(result);
    }
    return prefixed(head, std::move(result));
}

std::string shell_expand(std::string_view word, const envvars::state& env) {
    return unquote(word, &env);
}

std::string shell_unquote(std::string_view word) {
    return unquote(word, nullptr);
}

} // namespace uenv
