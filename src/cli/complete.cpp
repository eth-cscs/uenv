// vim: ts=4 sts=4 sw=4 et

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <argparse/argparse.h>
#include <argparse/complete.h>
#include <uenv/complete.h>
#include <uenv/log.h>
#include <uenv/meta.h>
#include <uenv/parse.h>
#include <uenv/repository.h>
#include <uenv/settings.h>

#include "cli.h"
#include "complete.h"
#include "uenv.h"

namespace uenv {

namespace {

struct complete_args {
    std::string cword;
    std::vector<std::string> words;
};

// The configuration and repositories, loaded the first time they are needed:
// completing a subcommand or option name reads neither.
class sources {
  public:
    sources(const global_settings& settings, global_args globals)
        : env_(settings.calling_environment), globals_(std::move(globals)) {
    }

    const envvars::state& env() const {
        return env_;
    }

    // the configuration, with --repo and --system from the command line
    const configuration* config() {
        if (!config_loaded_) {
            config_loaded_ = true;
            config_ = load();
        }
        return config_ ? &*config_ : nullptr;
    }

    std::optional<std::string> system() {
        auto c = config();
        return c ? c->system_name : std::nullopt;
    }

    // the repositories, in order of priority
    std::vector<repository>& repos() {
        if (!repos_loaded_) {
            repos_loaded_ = true;
            if (auto c = config()) {
                for (auto& r : c->repos) {
                    if (auto repo = open_repository(r.path)) {
                        repos_.push_back(std::move(*repo));
                    }
                }
            }
        }
        return repos_;
    }

    // every record in every repository
    const std::vector<uenv_record>& records() {
        if (!records_) {
            records_.emplace();
            for (auto& repo : repos()) {
                if (auto rs = repo.query(uenv_label{})) {
                    records_->insert(records_->end(), rs->begin(), rs->end());
                }
            }
        }
        return *records_;
    }

  private:
    const envvars::state& env_;
    global_args globals_;
    bool config_loaded_ = false;
    std::optional<configuration> config_;
    bool repos_loaded_ = false;
    std::vector<repository> repos_;
    std::optional<std::vector<uenv_record>> records_;

    std::optional<configuration> load() const {
        std::optional<std::vector<repo_label>> repo_labels;
        if (globals_.repo) {
            auto labels = parse_repo_list(*globals_.repo);
            if (!labels) {
                return std::nullopt;
            }
            repo_labels = *labels;
        }
        const config_base cli_config{.color = globals_.color,
                                     .system_name = globals_.system};
        // completion must not create the user's configuration file
        auto base = load_config(cli_config, repo_labels, env_,
                                user_config_mode::read_only);
        if (!base) {
            return std::nullopt;
        }
        return generate_configuration(*base);
    }
};

// The meta data of each uenv in a comma separated list of uenv descriptions,
// paired with the name of the uenv. The meta data is read only where it is on
// disk next to the image: it is not extracted from a squashfs file.
std::vector<std::pair<std::string, meta>> uenv_meta(std::string_view uenvs,
                                                    sources& src) {
    std::vector<std::pair<std::string, meta>> result;
    auto descriptions = parse_uenv_args(std::string(uenvs));
    if (!descriptions) {
        return result;
    }
    for (auto& desc : *descriptions) {
        std::optional<std::string> name;
        std::filesystem::path env_json;
        if (auto label = desc.label()) {
            auto query = apply_system(*label, src.system());
            for (auto& repo : src.repos()) {
                auto rs = repo.query(query);
                if (rs && !rs->empty()) {
                    auto& r = *rs->begin();
                    name = r.name;
                    env_json = repo.uenv_paths(r.sha).meta / "env.json";
                    break;
                }
            }
        } else if (auto file = desc.filename()) {
            env_json =
                std::filesystem::path(*file).parent_path() / "meta/env.json";
        }
        if (env_json.empty()) {
            continue;
        }
        if (auto m = load_meta(env_json)) {
            result.emplace_back(name.value_or(m->name), std::move(*m));
        }
    }
    return result;
}

// the uenvs given to the command that the cursor is in
std::string_view named_uenvs(const argparse::completion_request& req) {
    for (auto& it : req.context.items) {
        if (it.cmd == req.cmd && it.pos && it.value) {
            auto& c = it.pos->completer();
            if (c.type == argparse::completion::kind::custom &&
                (c.arg == "uenv_list" || c.arg == "uenv")) {
                return *it.value;
            }
        }
    }
    return {};
}

std::vector<candidate> complete_custom(const std::string& tag,
                                       const argparse::completion_request& req,
                                       sources& src) {
    auto prefix = req.prefix;
    if (tag == "local_label") {
        return complete_label(prefix, src.records(), src.system());
    }
    if (tag == "uenv") {
        return complete_uenv(prefix, src.records(), src.system(), src.env());
    }
    if (tag == "uenv_list") {
        return complete_uenv_list(prefix, src.records(), src.system(),
                                  src.env());
    }
    if (tag == "view_list") {
        return complete_views(prefix, uenv_meta(named_uenvs(req), src));
    }
    if (tag == "system") {
        return complete_system(prefix, src.records());
    }
    if (tag == "repo") {
        auto c = src.config();
        return complete_repo(prefix, c ? c->repos : repo_list{}, src.env());
    }
    if (tag == "shell") {
        std::vector<candidate> result;
        for (auto shell : {"bash", "zsh"}) {
            if (std::string_view(shell).starts_with(prefix)) {
                result.push_back({shell, {}});
            }
        }
        return result;
    }
    // registry_label: labels in a remote registry would need a cache, so
    // that completion never waits on the network.
    return {};
}

bool is_plain_path(const std::string& s) {
    return (s.starts_with('/') || s.starts_with('.') || s.starts_with('~')) &&
           s.find_first_of(",:=") == std::string::npos;
}

void print(const std::vector<candidate>& candidates,
           const std::vector<std::string>& directives) {
    for (auto& c : candidates) {
        // a candidate is one line, and the value and description are
        // separated by a tab: skip the file names that would break that
        if (c.value.find_first_of("\t\n") != std::string::npos) {
            continue;
        }
        std::string description = c.description;
        std::replace_if(
            description.begin(), description.end(),
            [](char ch) { return ch == '\t' || ch == '\n'; }, ' ');
        fmt::print("{}\t{}\n", c.value, description);
    }
    fmt::print(":{}\n", fmt::join(directives, ","));
}

} // namespace

int complete_main(const argparse::program<global_args>& cli,
                  const global_settings& settings,
                  std::span<const char* const> args) {
    // the output is read by the shell: nothing may be written to the terminal
    if (int fd = open("/dev/null", O_WRONLY); fd >= 0) {
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
    init_log(spdlog::level::off);

    argparse::command_builder<complete_args> builder("__complete",
                                                     "complete a command line");
    builder.add_option("cword", &complete_args::cword, "the word to complete")
        .required()
        .complete(argparse::completion::none());
    builder.add_rest("words", &complete_args::words, "the command line")
        .complete(argparse::completion::none());
    const argparse::program<complete_args> self(std::move(builder));

    std::vector<std::string_view> arg_words(args.begin(), args.end());
    auto invocation = self.parse(arg_words);
    std::size_t cword = 0;
    if (!invocation) {
        print({}, {});
        return 0;
    }
    const auto& a = invocation->globals();
    auto [end, ec] =
        std::from_chars(a.cword.data(), a.cword.data() + a.cword.size(), cword);
    // the program name is not completed
    if (ec != std::errc{} || end != a.cword.data() + a.cword.size() ||
        cword == 0 || a.words.empty()) {
        print({}, {});
        return 0;
    }

    // the words as the program would receive them, without the program name
    std::vector<std::string> words;
    for (auto& w : std::span(a.words).subspan(1)) {
        words.push_back(shell_unquote(w));
    }
    std::vector<std::string_view> views(words.begin(), words.end());

    auto req = argparse::complete(cli.root(), views, cword - 1);
    sources src(settings, cli.globals(req.context));

    auto candidates = std::move(req.candidates);
    std::vector<std::string> directives;
    bool paths = false;
    if (auto c = req.opt   ? &req.opt->completer()
                 : req.pos ? &req.pos->completer()
                           : nullptr) {
        using enum argparse::completion::kind;
        std::vector<candidate> values;
        switch (c->type) {
        case file:
            values = complete_path(
                req.prefix,
                {.files = true,
                 .glob = c->arg.empty() || c->arg == "*"
                             ? std::nullopt
                             : std::optional<std::string>(c->arg)},
                src.env());
            paths = true;
            break;
        case directory:
            values = complete_path(req.prefix, {.files = false}, src.env());
            paths = true;
            break;
        case path:
            values = complete_path(req.prefix, {}, src.env());
            paths = true;
            break;
        case command:
            // the shell's words include the program name
            directives.push_back(
                fmt::format("command-offset={}", req.rest_start + 1));
            break;
        case custom:
            values = complete_custom(c->arg, req, src);
            paths = std::all_of(
                values.begin(), values.end(),
                [](const candidate& v) { return is_plain_path(v.value); });
            break;
        default:
            break;
        }
        for (auto& v : values) {
            v.value.insert(0, req.keep);
        }
        candidates.insert(candidates.end(), values.begin(), values.end());
        if (paths && req.keep.empty() && !candidates.empty()) {
            directives.push_back("filenames");
        }
    }

    if (std::any_of(candidates.begin(), candidates.end(), [](auto& c) {
            return !c.value.empty() &&
                   std::string_view("/,:=@%").find(c.value.back()) !=
                       std::string_view::npos;
        })) {
        directives.push_back("nospace");
    }

    print(candidates, directives);
    return 0;
}

} // namespace uenv
