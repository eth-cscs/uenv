#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_all.hpp>

#include <argparse/argparse.h>

namespace {

using argparse::command_builder;
using argparse::completion;
using argparse::error_kind;
using argparse::item_kind;

//
// a small tree with the same shape as the uenv CLI
//

enum class fmt_choice { name, full, views };

struct global_args {
    int verbose = 0;
    std::optional<std::string> repo;
    std::optional<bool> color;
};

struct run_args {
    std::optional<std::string> view;
    std::string uenv;
    std::vector<std::string> commands;
    bool no_default_view = false;
    bool join = false;
};

struct ls_args {
    std::optional<std::string> uenv;
    bool json = false;
};

struct delete_args {
    std::string token;
    std::string uenv;
};

struct update_args {
    std::string repo;
    bool lustre = true;
};

struct status_args {
    fmt_choice format = fmt_choice::full;
};

struct cli {
    // the actions that have been run
    std::vector<std::string> ran;

    // each subcommand is built as a value, then added to its parent
    argparse::command run_command() {
        command_builder<run_args> run("run", "run a command");
        run.add_option({'v', "view"}, &run_args::view, "views")
            .complete(completion::custom("view_list"));
        run.add_positional("uenv", &run_args::uenv, "the uenv")
            .required()
            .complete(completion::custom("uenv_list"));
        run.add_rest("commands", &run_args::commands, "the command")
            .required()
            .complete(completion::command());
        run.add_flag({'V', "no-default-view"}, &run_args::no_default_view,
                     "no views");
        run.add_flag({'j', "join"}, &run_args::join, "join");
        run.action([this](const run_args&) {
            ran.push_back("run");
            return 3;
        });
        return std::move(run).build();
    }

    argparse::command image_command() {
        command_builder<ls_args> ls("ls", "list images");
        ls.add_positional("uenv", &ls_args::uenv, "search term")
            .complete(completion::custom("local_label"));
        ls.add_flag("json", &ls_args::json, "json output");
        ls.action([this](const ls_args&) {
            ran.push_back("ls");
            return 0;
        });

        command_builder<delete_args> del("delete", "delete an image");
        del.add_positional("uenv", &delete_args::uenv, "the uenv")
            .required()
            .complete(completion::custom("registry_label"));
        del.add_option("token", &delete_args::token, "a token")
            .required()
            .complete(completion::file());

        command_builder<> image("image", "manage images");
        image.add_subcommand(std::move(ls).build());
        image.add_subcommand(std::move(del).build());
        return std::move(image).build();
    }

    argparse::command repo_command() {
        command_builder<update_args> update("update", "update a repo");
        update.add_positional("repo", &update_args::repo, "the repo")
            .required()
            .complete(completion::path());
        update.add_flag("lustre", &update_args::lustre, "lustre")
            .negation("no-lustre");

        command_builder<> repo("repo", "manage repos");
        repo.add_subcommand(std::move(update).build());
        return std::move(repo).build();
    }

    argparse::command status_command() {
        command_builder<status_args> status("status", "status");
        status.add_choice("format", &status_args::format,
                          {{"short", fmt_choice::name},
                           {"full", fmt_choice::full},
                           {"views", fmt_choice::views}},
                          "the format");
        return std::move(status).build();
    }

    command_builder<global_args> root_command() {
        command_builder<global_args> root("prog", "a test program");
        root.add_flag({'v', "verbose"}, &global_args::verbose,
                      "verbose output");
        root.add_option("repo", &global_args::repo, "the repo")
            .complete(completion::path());
        root.add_flag("color", &global_args::color, "color output")
            .negation("no-color");
        root.add_subcommand(run_command());
        root.add_subcommand(image_command());
        root.add_subcommand(repo_command());
        root.add_subcommand(status_command());
        return root;
    }

    argparse::program<global_args> program{root_command()};

    const argparse::command& root() const {
        return program.root();
    }

    // the result views the words, so they are kept here
    std::vector<std::vector<std::string_view>> lines;

    argparse::parse_result parse(std::vector<std::string_view> words) {
        auto& w = lines.emplace_back(std::move(words));
        return argparse::parse(root(), std::span<const std::string_view>(w));
    }

    util::expected<argparse::invocation<global_args>, argparse::error>
    invoke(std::vector<std::string_view> words) {
        return program.parse(std::span<const std::string_view>(words));
    }

    cli() = default;
    cli(const cli&) = delete;
};

std::vector<error_kind> kinds(const argparse::parse_result& r) {
    std::vector<error_kind> k;
    for (auto& e : r.errors) {
        k.push_back(e.kind);
    }
    return k;
}

// the values of the selected command after parsing words, which must be valid
template <typename T> T values_of(cli& c, std::vector<std::string_view> words) {
    auto inv = c.invoke(std::move(words));
    REQUIRE(inv);
    REQUIRE(!inv->help_requested());
    auto v = inv->template selected_values<T>();
    REQUIRE(v);
    return *v;
}

global_args globals_of(cli& c, std::vector<std::string_view> words) {
    auto inv = c.invoke(std::move(words));
    REQUIRE(inv);
    return inv->globals();
}

} // namespace

TEST_CASE("the test tree is valid", "[argparse]") {
    cli c;
    auto v = c.program.validate();
    INFO((v ? "" : v.error()));
    REQUIRE(v);
}

TEST_CASE("empty command line", "[argparse]") {
    cli c;
    auto r = c.parse({});
    REQUIRE(r.ok());
    REQUIRE(r.errors.empty());
    REQUIRE(r.items.empty());
    REQUIRE(&r.selected() == &c.root());

    auto inv = c.invoke({});
    REQUIRE(inv);
    REQUIRE(&inv->selected() == &c.root());
    REQUIRE(!inv->has_action());
    // the defaults of the global options
    REQUIRE(inv->globals().verbose == 0);
    REQUIRE(!inv->globals().repo);
    REQUIRE(!inv->globals().color);
}

TEST_CASE("subcommands", "[argparse]") {
    cli c;
    auto r = c.parse({"image", "ls"});
    REQUIRE(r.ok());
    REQUIRE(r.path.size() == 3u);
    REQUIRE(r.selected().name() == "ls");
    REQUIRE(r.items.size() == 2u);
    REQUIRE(r.items[0].kind == item_kind::subcommand);
    REQUIRE(r.items[1].kind == item_kind::subcommand);

    auto inv = c.invoke({"image", "ls"});
    REQUIRE(inv);
    REQUIRE(inv->selected().name() == "ls");
    REQUIRE(!inv->help_requested());
    // parsing does not run the action
    REQUIRE(c.ran.empty());
    REQUIRE(inv->has_action());
    REQUIRE(inv->run() == 0);
    REQUIRE(c.ran == std::vector<std::string>{"ls"});

    // a command with subcommands does not require one to be given, and need
    // not have an action
    auto image = c.invoke({"image"});
    REQUIRE(image);
    REQUIRE(image->selected().name() == "image");
    REQUIRE(!image->has_action());
    REQUIRE(image->selected_values<argparse::no_args>());

    auto bad = c.parse({"image", "lx"});
    REQUIRE(kinds(bad) == std::vector{error_kind::unknown_command});
    REQUIRE(bad.errors[0].word == 1u);
    REQUIRE(bad.errors[0].message.find("'lx'") != std::string::npos);
    REQUIRE(c.invoke({"image", "lx"}).error().kind ==
            error_kind::unknown_command);
}

TEST_CASE("values", "[argparse]") {
    cli c;
    auto inv = c.invoke({"-vv", "--repo=/r", "--color", "run", "-V", "--view",
                         "x", "img", "cmd"});
    REQUIRE(inv);
    auto& g = inv->globals();
    REQUIRE(g.verbose == 2);
    REQUIRE(g.repo == "/r");
    REQUIRE(g.color == true);

    auto v = inv->selected_values<run_args>();
    REQUIRE(v);
    REQUIRE(v->view == "x");
    REQUIRE(v->uenv == "img");
    REQUIRE(v->commands == std::vector<std::string>{"cmd"});
    REQUIRE(v->no_default_view);
    REQUIRE(!v->join);

    // the values of another type are not available
    REQUIRE(!inv->selected_values<ls_args>());

    REQUIRE(c.ran.empty());
    REQUIRE(inv->run() == 3);
    REQUIRE(c.ran == std::vector<std::string>{"run"});
}

TEST_CASE("every parse makes new values", "[argparse]") {
    cli c;
    auto first = c.invoke({"run", "-V", "a", "cmd"});
    auto second = c.invoke({"run", "b", "cmd"});
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first->selected_values<run_args>()->uenv == "a");
    REQUIRE(first->selected_values<run_args>()->no_default_view);
    REQUIRE(second->selected_values<run_args>()->uenv == "b");
    REQUIRE(!second->selected_values<run_args>()->no_default_view);

    // an invocation is a value
    auto copy = *first;
    REQUIRE(copy.selected_values<run_args>()->uenv == "a");
    REQUIRE(copy.selected_values<run_args>() !=
            first->selected_values<run_args>());
}

TEST_CASE("defaults", "[argparse]") {
    struct args {
        int count = 7;
        std::string name = "default";
    };
    command_builder<args> b("prog", "", args{.count = 3, .name = "given"});
    b.add_flag({'c', "count"}, &args::count, "");
    b.add_option("name", &args::name, "").complete(completion::none());
    argparse::program<args> p(std::move(b));

    std::vector<std::string_view> none;
    REQUIRE(p.parse(none)->globals().count == 3);
    REQUIRE(p.parse(none)->globals().name == "given");

    std::vector<std::string_view> words{"-cc", "--name", "x"};
    REQUIRE(p.parse(words)->globals().count == 2);
    REQUIRE(p.parse(words)->globals().name == "x");
}

TEST_CASE("long options", "[argparse]") {
    cli c;
    SECTION("value in the next word") {
        auto r = c.parse({"--repo", "/a"});
        REQUIRE(r.ok());
        REQUIRE(r.items.size() == 2u);
        REQUIRE(r.items[0].kind == item_kind::option);
        REQUIRE(!r.items[0].value);
        REQUIRE(r.items[1].kind == item_kind::option_value);
        REQUIRE(r.items[1].value == "/a");
        REQUIRE(globals_of(c, {"--repo", "/a"}).repo == "/a");
    }
    SECTION("value in the same word") {
        auto r = c.parse({"--repo=/a=b"});
        REQUIRE(r.ok());
        REQUIRE(r.items.size() == 1u);
        REQUIRE(r.items[0].value == "/a=b");
        REQUIRE(globals_of(c, {"--repo=/a=b"}).repo == "/a=b");
    }
    SECTION("empty value in the same word") {
        REQUIRE(globals_of(c, {"--repo="}).repo == "");
    }
    SECTION("a value that looks like an option") {
        auto g = globals_of(c, {"--repo", "--color"});
        REQUIRE(g.repo == "--color");
        REQUIRE(!g.color);
    }
    SECTION("missing value") {
        auto r = c.parse({"--repo"});
        REQUIRE(kinds(r) == std::vector{error_kind::missing_value});
        REQUIRE(r.pending == c.root().find_long("repo"));
        REQUIRE(!c.invoke({"--repo"}));
    }
    SECTION("unknown option") {
        auto r = c.parse({"--bogus=12"});
        REQUIRE(kinds(r) == std::vector{error_kind::unknown_option});
        REQUIRE(r.errors[0].message.find("'--bogus'") != std::string::npos);
        REQUIRE(r.items[0].kind == item_kind::unrecognised);
    }
    SECTION("a flag does not take a value") {
        auto r = c.parse({"image", "ls", "--json=false"});
        REQUIRE(kinds(r) == std::vector{error_kind::flag_with_value});
    }
    SECTION("repeated option") {
        auto r = c.parse({"--repo", "a", "--repo=b"});
        REQUIRE(kinds(r) == std::vector{error_kind::repeated_option});
        REQUIRE(r.errors[0].word == 2u);
    }
}

TEST_CASE("flags", "[argparse]") {
    cli c;
    SECTION("counting") {
        REQUIRE(globals_of(c, {"-v", "--verbose", "-vv"}).verbose == 4);
    }
    SECTION("a counter that is not given keeps its default") {
        REQUIRE(globals_of(c, {}).verbose == 0);
    }
    SECTION("repeated boolean flags are allowed") {
        REQUIRE(
            values_of<ls_args>(c, {"image", "ls", "--json", "--json"}).json);
    }
    SECTION("negation: the last one wins") {
        auto r = c.parse({"repo", "update", "x", "--no-lustre"});
        REQUIRE(r.ok());
        REQUIRE(r.items.back().negated);
        REQUIRE(
            !values_of<update_args>(c, {"repo", "update", "x", "--no-lustre"})
                 .lustre);
        REQUIRE(values_of<update_args>(
                    c, {"repo", "update", "--no-lustre", "x", "--lustre"})
                    .lustre);
        REQUIRE(values_of<update_args>(c, {"repo", "update", "x"}).lustre);
    }
    SECTION("an optional flag: unset, or the last one given") {
        REQUIRE(!globals_of(c, {}).color);
        REQUIRE(globals_of(c, {"--color", "--no-color"}).color == false);
        REQUIRE(globals_of(c, {"--no-color", "--color", "--no-color"}).color ==
                false);
        REQUIRE(globals_of(c, {"--no-color", "--color"}).color == true);
    }
}

TEST_CASE("short options", "[argparse]") {
    cli c;
    SECTION("cluster of flags") {
        auto v = values_of<run_args>(c, {"run", "-Vj", "img", "cmd"});
        REQUIRE(v.no_default_view);
        REQUIRE(v.join);
    }
    SECTION("attached value") {
        auto v = values_of<run_args>(c, {"run", "-Vvdefault", "img", "cmd"});
        REQUIRE(v.no_default_view);
        REQUIRE(v.view == "default");
    }
    SECTION("value in the next word") {
        auto v =
            values_of<run_args>(c, {"run", "-jv", "default", "img", "cmd"});
        REQUIRE(v.join);
        REQUIRE(v.view == "default");
    }
    SECTION("options are scoped to their command") {
        // -v is --verbose at the root, and --view under run
        auto inv = c.invoke({"-v", "run", "-v", "x", "img", "cmd"});
        REQUIRE(inv);
        REQUIRE(inv->globals().verbose == 1);
        REQUIRE(inv->selected_values<run_args>()->view == "x");

        REQUIRE(kinds(c.parse({"image", "ls", "--verbose"})) ==
                std::vector{error_kind::unknown_option});
    }
    SECTION("unknown option in a cluster") {
        auto r = c.parse({"run", "-Vx", "img", "cmd"});
        REQUIRE(kinds(r) == std::vector{error_kind::unknown_option});
        REQUIRE(r.errors[0].message.find("'-x'") != std::string::npos);
    }
    SECTION("a lone dash is a positional argument") {
        REQUIRE(values_of<ls_args>(c, {"image", "ls", "-"}).uenv == "-");
    }
}

TEST_CASE("positionals", "[argparse]") {
    cli c;
    SECTION("optional positional") {
        auto r = c.parse({"image", "ls"});
        REQUIRE(r.ok());
        REQUIRE(r.next_positional == 0u);
        REQUIRE(!values_of<ls_args>(c, {"image", "ls"}).uenv);
    }
    SECTION("too many") {
        auto r = c.parse({"image", "ls", "a", "b"});
        REQUIRE(kinds(r) == std::vector{error_kind::unexpected_argument});
        REQUIRE(r.errors[0].word == 3u);
    }
    SECTION("required") {
        REQUIRE(kinds(c.parse({"run"})) ==
                std::vector{error_kind::missing_positional,
                            error_kind::missing_positional});
        REQUIRE(kinds(c.parse({"run", "img"})) ==
                std::vector{error_kind::missing_positional});
    }
    SECTION("rest takes every remaining word") {
        std::vector<std::string_view> words{"run",    "img", "ls", "-l",
                                            "--view", "x",   "--"};
        auto r = c.parse(words);
        REQUIRE(r.ok());
        REQUIRE(r.in_rest);
        auto v = values_of<run_args>(c, words);
        REQUIRE(v.uenv == "img");
        REQUIRE(!v.view);
        REQUIRE(v.commands ==
                std::vector<std::string>{"ls", "-l", "--view", "x", "--"});
    }
    SECTION("end of options") {
        auto r = c.parse({"run", "--", "-img", "--view"});
        REQUIRE(r.ok());
        REQUIRE(r.end_of_options);
        REQUIRE(r.items[1].kind == item_kind::end_of_options);
        auto v = values_of<run_args>(c, {"run", "--", "-img", "--view"});
        REQUIRE(v.uenv == "-img");
        REQUIRE(v.commands == std::vector<std::string>{"--view"});
    }
    SECTION("options between positionals") {
        auto v = values_of<run_args>(c, {"run", "img", "-V", "--", "cmd"});
        REQUIRE(v.no_default_view);
        REQUIRE(v.commands == std::vector<std::string>{"cmd"});
    }
    SECTION("-- in a command with subcommands") {
        REQUIRE(kinds(c.parse({"image", "--", "ls"})) ==
                std::vector{error_kind::unexpected_argument});
    }
    SECTION("empty words") {
        auto v = values_of<run_args>(c, {"run", "", ""});
        REQUIRE(v.uenv == "");
        REQUIRE(v.commands == std::vector<std::string>{""});
        REQUIRE(kinds(c.parse({""})) ==
                std::vector{error_kind::unknown_command});
    }
}

TEST_CASE("required options", "[argparse]") {
    cli c;
    REQUIRE(kinds(c.parse({"image", "delete", "x"})) ==
            std::vector{error_kind::missing_option});
    auto v =
        values_of<delete_args>(c, {"image", "delete", "--token", "t", "x"});
    REQUIRE(v.token == "t");
    REQUIRE(v.uenv == "x");
}

TEST_CASE("choices", "[argparse]") {
    cli c;
    REQUIRE(values_of<status_args>(c, {"status"}).format == fmt_choice::full);
    REQUIRE(values_of<status_args>(c, {"status", "--format=short"}).format ==
            fmt_choice::name);
    REQUIRE(values_of<status_args>(c, {"status", "--format", "views"}).format ==
            fmt_choice::views);

    auto r = c.parse({"status", "--format", "name"});
    REQUIRE(kinds(r) == std::vector{error_kind::invalid_choice});
    REQUIRE(c.invoke({"status", "--format", "name"}).error().kind ==
            error_kind::invalid_choice);

    auto o = c.root().find_subcommand("status")->find_long("format");
    REQUIRE(o->completer().type == completion::kind::choice);
    REQUIRE(o->choices() == std::vector<std::string>{"short", "full", "views"});
}

TEST_CASE("help", "[argparse]") {
    cli c;
    for (auto words : std::vector<std::vector<std::string_view>>{
             {"image", "ls", "--help"},
             {"image", "ls", "-h", "--bogus"},
             {"image", "ls", "--bogus", "-h"},
             {"image", "delete", "-h"}}) {
        auto r = c.parse(words);
        REQUIRE(r.ok());
        REQUIRE(r.help == &r.selected());
        auto inv = c.invoke(words);
        REQUIRE(inv);
        REQUIRE(inv->help_requested());
        REQUIRE(inv->help() == argparse::render_help(r.selected()));
    }
    // help for the command -h was given to, not the selected one
    auto inv = c.invoke({"-h", "image", "ls"});
    REQUIRE(inv->help_requested());
    REQUIRE(inv->help() == argparse::render_help(c.root()));
    // without -h, help() describes the selected command
    auto image = c.invoke({"image"});
    REQUIRE(!image->help_requested());
    REQUIRE(image->help() ==
            argparse::render_help(*c.root().find_subcommand("image")));

    auto text = argparse::render_help(*c.root().find_subcommand("run"));
    REQUIRE(text.find("Usage: prog run [OPTIONS] uenv commands...") !=
            std::string::npos);
    REQUIRE(text.find("-v, --view VIEW") != std::string::npos);
    REQUIRE(text.find("-h, --help") != std::string::npos);
    REQUIRE(text.find("(required)") != std::string::npos);

    auto root = argparse::render_help(c.root());
    REQUIRE(root.find("Usage: prog [OPTIONS] SUBCOMMAND") != std::string::npos);
    REQUIRE(root.find("Subcommands:") != std::string::npos);
    REQUIRE(root.find("--color, --no-color") != std::string::npos);

    auto update = argparse::render_help(
        *c.root().find_subcommand("repo")->find_subcommand("update"));
    REQUIRE(update.find("--lustre, --no-lustre") != std::string::npos);

    auto status = argparse::render_help(*c.root().find_subcommand("status"));
    REQUIRE(status.find("--format {short,full,views}") != std::string::npos);
}

TEST_CASE("partial command lines", "[argparse]") {
    cli c;
    SECTION("an option waiting for its value") {
        auto r = c.parse({"run", "--view"});
        REQUIRE(r.pending);
        REQUIRE(r.pending->long_name() == "view");
        REQUIRE(r.selected().name() == "run");
    }
    SECTION("next positional") {
        auto r = c.parse({"run", "-V"});
        REQUIRE(!r.pending);
        REQUIRE(r.next_positional == 0u);
        r = c.parse({"run", "img"});
        REQUIRE(r.next_positional == 1u);
        REQUIRE(!r.in_rest);
        r = c.parse({"run", "img", "cmd"});
        REQUIRE(r.in_rest);
    }
    SECTION("parsing continues after an error") {
        auto r = c.parse({"--bogus", "run", "img", "cmd"});
        REQUIRE(kinds(r) == std::vector{error_kind::unknown_option});
        REQUIRE(r.selected().name() == "run");
        REQUIRE(r.in_rest);
    }
    SECTION("every word is accounted for, in order") {
        auto r = c.parse({"-vv", "--repo", "x", "run", "-Vvview", "--", "img",
                          "a", "b", "--bogus"});
        std::vector<std::size_t> words;
        for (auto& it : r.items) {
            if (words.empty() || words.back() != it.word) {
                words.push_back(it.word);
            }
        }
        REQUIRE(words ==
                std::vector<std::size_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
    }
}

TEST_CASE("actions", "[argparse]") {
    struct args {
        std::string name;
    };
    std::string seen;

    command_builder<args> leaf("leaf", "");
    leaf.add_positional("name", &args::name, "")
        .required()
        .complete(completion::none());
    // an action can be given the command it belongs to
    leaf.action([&seen](const args& a, const argparse::command& self) {
        seen = a.name + "@" + self.parent()->name();
        return 5;
    });
    command_builder<> root("prog", "");
    root.add_subcommand(std::move(leaf).build());
    argparse::program<argparse::no_args> p(std::move(root));

    std::vector<std::string_view> words{"leaf", "x"};
    auto inv = p.parse(words);
    REQUIRE(inv);
    REQUIRE(seen.empty());
    REQUIRE(inv->run() == 5);
    REQUIRE(seen == "x@prog");
}

TEST_CASE("moving commands", "[argparse]") {
    struct top_args {
        bool x = false;
    };
    struct leaf_args {
        std::string p;
    };

    // build a three level tree bottom up, as values
    auto make_tree = []() {
        command_builder<leaf_args> c("c", "leaf");
        c.add_positional("p", &leaf_args::p, "")
            .required()
            .complete(completion::none());
        command_builder<> b("b", "middle");
        b.add_subcommand(std::move(c).build());
        command_builder<top_args> a("a", "top");
        a.add_flag("x", &top_args::x, "");
        a.add_subcommand(std::move(b).build());
        return std::move(a).build();
    };

    auto check = [](const argparse::command& root) {
        auto a = root.find_subcommand("a");
        REQUIRE(a);
        REQUIRE(a->parent() == &root);
        auto b = a->find_subcommand("b");
        REQUIRE(b);
        REQUIRE(b->parent() == a);
        auto c = b->find_subcommand("c");
        REQUIRE(c);
        REQUIRE(c->parent() == b);
        REQUIRE(c->path() == std::vector<std::string>{"prog", "a", "b", "c"});
    };

    SECTION("add_subcommand re-links the moved subtree") {
        command_builder<> root("prog", "");
        auto& added = root.add_subcommand(make_tree());
        argparse::program<argparse::no_args> p(std::move(root));
        REQUIRE(&added == p.root().find_subcommand("a"));
        REQUIRE(p.validate());
        check(p.root());

        std::vector<std::string_view> words{"a", "--x", "b", "c", "val"};
        auto inv = p.parse(words);
        REQUIRE(inv);
        REQUIRE(inv->selected().name() == "c");
        REQUIRE(inv->selected_values<leaf_args>()->p == "val");
    }
    SECTION("move construction") {
        command_builder<> root("prog", "");
        root.add_subcommand(make_tree());
        auto built = std::move(root).build();
        argparse::command moved(std::move(built));
        check(moved);
    }
    SECTION("move assignment") {
        command_builder<> root("prog", "");
        root.add_subcommand(make_tree());
        auto other = command_builder<>("other", "").build();
        other = std::move(root).build();
        REQUIRE(other.name() == "prog");
        check(other);
    }
}

TEST_CASE("validation", "[argparse]") {
    struct args {
        std::string s;
        std::optional<std::string> o;
        std::vector<std::string> v;
        bool b = false;
        int i = 0;
    };
    using builder = command_builder<args>;

    auto invalid = [](builder c, std::string_view what) {
        auto cmd = std::move(c).build();
        auto r = cmd.validate();
        REQUIRE(!r);
        INFO(r.error());
        REQUIRE(r.error().find(what) != std::string::npos);
    };

    {
        builder c("prog", "");
        c.add_subcommand(builder("a", "").build());
        c.add_positional("p", &args::s, "").complete(completion::none());
        invalid(std::move(c), "both subcommands and positional");
    }
    {
        builder c("prog", "");
        c.add_subcommand(builder("a", "").build());
        c.add_subcommand(builder("a", "").build());
        invalid(std::move(c), "duplicate subcommand 'a'");
    }
    {
        builder c("prog", "");
        c.add_flag("x", &args::b, "");
        c.add_option("x", &args::o, "").complete(completion::none());
        invalid(std::move(c), "duplicate option '--x'");
    }
    {
        builder c("prog", "");
        c.add_flag({'h', "x"}, &args::b, "");
        invalid(std::move(c), "duplicate option '-h'");
    }
    {
        builder c("prog", "");
        c.add_flag("help", &args::b, "");
        invalid(std::move(c), "duplicate option '--help'");
    }
    {
        builder c("prog", "");
        c.add_flag("x", &args::b, "").negation("help");
        invalid(std::move(c), "duplicate option '--help'");
    }
    {
        builder c("prog", "");
        c.add_option("x", &args::o, "")
            .negation("no-x")
            .complete(completion::none());
        invalid(std::move(c), "only a boolean flag can have a negation");
    }
    {
        builder c("prog", "");
        c.add_flag("x", &args::i, "").negation("no-x");
        invalid(std::move(c), "only a boolean flag can have a negation");
    }
    {
        builder c("prog", "");
        c.add_option("x", &args::o, "");
        invalid(std::move(c), "option --x has no completion");
    }
    {
        builder c("prog", "");
        c.add_flag("x", &args::b, "").required();
        invalid(std::move(c), "flag --x can't be required");
    }
    {
        builder c("prog", "");
        c.add_flag("", &args::b, "");
        invalid(std::move(c), "an option has no name");
    }
    for (auto name : {"-x", "x=y", "x y", "é", "--"}) {
        builder c("prog", "");
        c.add_flag(std::string(name), &args::b, "");
        invalid(std::move(c), "invalid option name");
    }
    {
        builder c("prog", "");
        c.add_flag('-', &args::b, "");
        invalid(std::move(c), "invalid short option name");
    }
    {
        builder c("prog", "");
        c.add_rest("r", &args::v, "").complete(completion::none());
        c.add_positional("p", &args::s, "").complete(completion::none());
        invalid(std::move(c), "must be last");
    }
    {
        builder c("prog", "");
        c.add_positional("p", &args::o, "").complete(completion::none());
        c.add_positional("q", &args::s, "")
            .required()
            .complete(completion::none());
        invalid(std::move(c), "follows an optional one");
    }
    {
        builder c("prog", "");
        c.add_positional("p", &args::s, "");
        invalid(std::move(c), "positional 'p' has no completion");
    }
    {
        builder c("prog", "");
        builder a("a", "");
        a.add_subcommand(builder("-b", "").build());
        c.add_subcommand(std::move(a).build());
        invalid(std::move(c), "prog a -b: invalid command name");
    }
}
