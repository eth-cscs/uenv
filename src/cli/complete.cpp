// vim: ts=4 sts=4 sw=4 et

#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <map>
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
#include <site/site.h>
#include <uenv/complete.h>
#include <uenv/log.h>
#include <uenv/meta.h>
#include <uenv/parse.h>
#include <uenv/repository.h>
#include <uenv/settings.h>
#include <util/curl.h>
#include <util/detach.h>

#include "cli.h"
#include "complete.h"
#include "uenv.h"

namespace uenv {

namespace {

// the time a detached refresh of a registry listing may take before it is
// killed: util::curl::get gives up after 5s
constexpr auto listing_refresh_limit = std::chrono::seconds(15);

struct complete_args {
    std::string cword;
    std::vector<std::string> words;
};

// The configuration, repositories and cached registry listings, loaded the
// first time they are needed: completing a subcommand or option name reads
// none of them.
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

    // the namespace of a registry label that has none
    std::optional<std::string> default_namespace() {
        auto c = config();
        if (!c || !c->registry) {
            return std::nullopt;
        }
        return c->registry->default_namespace;
    }

    // the namespaces of the registry: the site's, and those in the cache
    std::vector<std::string> namespaces() {
        std::vector<std::string> result(std::begin(site::registry_namespaces),
                                        std::end(site::registry_namespaces));
        if (auto nspace = default_namespace()) {
            result.push_back(*nspace);
        }
        if (auto dir = listing_cache()) {
            auto cached = site::cached_namespaces(*dir);
            result.insert(result.end(), cached.begin(), cached.end());
        }
        return result;
    }

    // The records of a namespace in the cached listing of the registry. The
    // registry is never queried here: if the listing is due a refresh, it is
    // fetched by a detached process, for the next completion to use.
    const std::vector<uenv_record>& listing(const std::string& nspace) {
        auto it = listings_.find(nspace);
        if (it == listings_.end()) {
            it = listings_.emplace(nspace, std::vector<uenv_record>{}).first;
            if (auto dir = listing_cache()) {
                auto records = site::cached_registry_listing(*dir, nspace);
                const bool usable = records.has_value();
                if (usable) {
                    it->second = std::move(*records);
                }
                refresh(*dir, nspace, usable);
            }
        }
        return it->second;
    }

    // whether a registry is configured
    bool has_registry() {
        auto c = config();
        return c && c->registry;
    }

  private:
    const envvars::state& env_;
    global_args globals_;
    bool config_loaded_ = false;
    std::optional<configuration> config_;
    bool repos_loaded_ = false;
    std::vector<repository> repos_;
    std::optional<std::vector<uenv_record>> records_;
    std::map<std::string, std::vector<uenv_record>> listings_;

    std::optional<std::filesystem::path> listing_cache() {
        auto c = config();
        auto root = user_cache_path(env_);
        if (!c || !c->registry || !root) {
            return std::nullopt;
        }
        return site::listing_cache_dir(*root, c->registry->listing_url);
    }

    // Start a detached refresh of the cached listing of `nspace`, if it is
    // due. A namespace that has no usable listing is only fetched if it is
    // one of namespaces(), so that a namespace that was only typed is not.
    void refresh(const std::filesystem::path& dir, const std::string& nspace,
                 bool usable) {
        auto root = user_cache_path(env_);
        auto c = config();
        if (!root || !c || !c->registry) {
            return;
        }
        if (!usable) {
            auto known = namespaces();
            if (std::find(known.begin(), known.end(), nspace) == known.end()) {
                return;
            }
        }
        if (!site::claim_listing_refresh(dir, nspace, usable)) {
            return;
        }
        util::spawn_detached(
            [&env = env_, url = c->registry->listing_url, nspace,
             root = *root]() {
                // complete_main runs before main() has configured TLS
                util::curl::configure_tls(env);
                site::refresh_registry_listing(url, nspace, root);
            },
            listing_refresh_limit);
    }

    std::optional<configuration> load() const {
        // completion must not create the user's configuration file
        auto loaded =
            load_configuration(globals_, env_, user_config_mode::read_only);
        if (!loaded) {
            return std::nullopt;
        }
        return std::move(loaded->config);
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

// the value given to the command that the cursor is in, for the positional
// argument whose values are completed with one of the custom `tags`
std::optional<std::string_view>
positional_value(const argparse::completion_request& req,
                 std::initializer_list<std::string_view> tags) {
    for (auto& it : req.context.items) {
        if (it.cmd == req.cmd && it.pos && it.value) {
            auto& c = it.pos->completer();
            if (c.type == argparse::completion::kind::custom &&
                std::find(tags.begin(), tags.end(), c.arg) != tags.end()) {
                return *it.value;
            }
        }
    }
    return std::nullopt;
}

// the uenvs given to the command that the cursor is in
std::string_view named_uenvs(const argparse::completion_request& req) {
    return positional_value(req, {"uenv_list", "uenv"}).value_or("");
}

// The records of the uenv that is copied (a label in the registry) or pushed
// (a label in a local repository) by the command that the cursor is in.
std::vector<uenv_record> copied_uenv(const argparse::completion_request& req,
                                     sources& src) {
    std::vector<uenv_record> result;
    if (auto source = positional_value(req, {"registry_nslabel"})) {
        // uenv image copy: the source is looked up as image_copy does
        auto nslabel = parse_uenv_nslabel(std::string(*source));
        if (!nslabel || !nslabel->nspace) {
            return result;
        }
        auto store = create_repository();
        if (!store) {
            return result;
        }
        for (auto& r : src.listing(*nslabel->nspace)) {
            store->add(r);
        }
        if (auto rs = store->query(nslabel->label)) {
            result.assign(rs->begin(), rs->end());
        }
    } else if (auto source = positional_value(req, {"uenv"})) {
        // uenv image push: only a label has a name and version to offer
        auto desc = parse_uenv_description(std::string(*source));
        if (!desc || !desc->label()) {
            return result;
        }
        auto query = apply_system(*desc->label(), src.system());
        for (auto& repo : src.repos()) {
            if (auto rs = repo.query(query); rs && !rs->empty()) {
                result.assign(rs->begin(), rs->end());
                break;
            }
        }
    }
    return result;
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
    // labels in a remote registry are read from the listings cached by the
    // commands that fetched them: completion never waits on the network
    if (tag == "registry_label" || tag == "registry_nslabel") {
        if (!src.has_registry()) {
            return {};
        }
        return complete_registry_label(
            prefix, src.namespaces(),
            tag == "registry_label" ? src.default_namespace() : std::nullopt,
            [&src](const std::string& nspace) { return src.listing(nspace); },
            src.system());
    }
    if (tag == "registry_dest") {
        if (!src.has_registry()) {
            return {};
        }
        return complete_registry_destination(prefix, src.namespaces(),
                                             copied_uenv(req, src));
    }
    return {};
}

// a path that the shell can quote as a file name: bash would also quote the
// $ of a variable, so paths that contain one are not
bool is_plain_path(const std::string& s) {
    return (s.starts_with('/') || s.starts_with('.') || s.starts_with('~')) &&
           s.find_first_of(",:=$") == std::string::npos;
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
    (void)util::redirect_to_null({STDERR_FILENO});
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

    // the words as the program would receive them, without the program
    // name. The word at the cursor is not expanded: the candidates for it
    // start with the text that was typed, e.g. ~/ or $HOME/
    std::vector<std::string> words;
    for (std::size_t i = 1; i < a.words.size(); ++i) {
        words.push_back(
            i == cword
                ? shell_unquote(a.words[i])
                : shell_expand(a.words[i], settings.calling_environment));
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
        // bash would quote the $ of a variable in a file name
        const bool variables = std::any_of(
            candidates.begin(), candidates.end(), [](const candidate& v) {
                return v.value.find('$') != std::string::npos;
            });
        if (paths && req.keep.empty() && !candidates.empty() && !variables) {
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
