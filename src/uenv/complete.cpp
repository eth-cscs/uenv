#include <fnmatch.h>

#include <algorithm>
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
    return s.starts_with('/') || s.starts_with('.') || s.starts_with('~');
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
    auto [dir_text, base] = split_last(prefix, '/');
    std::string dir(dir_text);
    if (dir.starts_with("~/")) {
        auto home = env.get("HOME");
        if (!home) {
            return {};
        }
        dir = *home + dir.substr(1);
    }
    if (dir.empty()) {
        dir = ".";
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

std::string shell_unquote(std::string_view word) {
    std::string result;
    enum { none, single, dbl } quote = none;
    for (std::size_t i = 0; i < word.size(); ++i) {
        const char c = word[i];
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

} // namespace uenv
