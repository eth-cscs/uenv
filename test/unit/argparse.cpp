#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_all.hpp>

#include <argparse/argparse.h>

namespace {

using namespace std::string_view_literals;
using argparse::completion;
using argparse::error_kind;
using argparse::item_kind;

enum class fmt_choice { name, full, views };

// a small tree with the same shape as the uenv CLI
struct cli {
    argparse::command root{"prog", "a test program"};

    int verbose = 0;
    std::optional<std::string> repo;
    std::optional<bool> color;

    std::optional<std::string> view;
    std::string uenv;
    std::vector<std::string> commands;
    bool no_default_view = false;
    bool join = false;

    std::optional<std::string> ls_uenv;
    bool json = false;

    std::string token;
    std::string delete_uenv;

    std::string update_repo;
    bool lustre = true;

    fmt_choice format = fmt_choice::full;

    std::vector<std::string> selected;

    cli() {
        root.add_flag({'v', "verbose"}, verbose, "verbose output");
        root.add_option("repo", repo, "the repo").complete(completion::path());
        root.add_flag("color", [this] { color = true; }, "enable color");
        root.add_flag("no-color", [this] { color = false; }, "disable color");

        auto& run = root.add_subcommand("run", "run a command");
        run.add_option({'v', "view"}, view, "views")
            .complete(completion::custom("view_list"));
        run.add_positional("uenv", uenv, "the uenv")
            .required()
            .complete(completion::custom("uenv_list"));
        run.add_rest("commands", commands, "the command")
            .required()
            .complete(completion::command());
        run.add_flag({'V', "no-default-view"}, no_default_view, "no views");
        run.add_flag({'j', "join"}, join, "join");
        run.on_selected([this] { selected.push_back("run"); });

        auto& image = root.add_subcommand("image", "manage images");
        image.on_selected([this] { selected.push_back("image"); });
        auto& ls = image.add_subcommand("ls", "list images");
        ls.add_positional("uenv", ls_uenv, "search term")
            .complete(completion::custom("local_label"));
        ls.add_flag("json", json, "json output");
        ls.on_selected([this] { selected.push_back("ls"); });
        auto& del = image.add_subcommand("delete", "delete an image");
        del.add_positional("uenv", delete_uenv, "the uenv")
            .required()
            .complete(completion::custom("registry_label"));
        del.add_option("token", token, "a token")
            .required()
            .complete(completion::file());

        auto& repo_cmd = root.add_subcommand("repo", "manage repos");
        auto& update = repo_cmd.add_subcommand("update", "update a repo");
        update.add_positional("repo", update_repo, "the repo")
            .required()
            .complete(completion::path());
        update.add_flag("lustre", lustre, "lustre").negation("no-lustre");

        auto& status = root.add_subcommand("status", "status");
        status.add_choice("format", format,
                          {{"short", fmt_choice::name},
                           {"full", fmt_choice::full},
                           {"views", fmt_choice::views}},
                          "the format");
    }

    argparse::parse_result parse(std::vector<std::string_view> words) {
        return argparse::parse(root, std::span<const std::string_view>(words));
    }
};

std::vector<error_kind> kinds(const argparse::parse_result& r) {
    std::vector<error_kind> k;
    for (auto& e : r.errors) {
        k.push_back(e.kind);
    }
    return k;
}

} // namespace

TEST_CASE("the test tree is valid", "[argparse]") {
    cli c;
    auto v = c.root.validate();
    INFO((v ? "" : v.error()));
    REQUIRE(v);
}

TEST_CASE("empty command line", "[argparse]") {
    cli c;
    auto r = c.parse({});
    REQUIRE(r.ok());
    REQUIRE(r.errors.empty());
    REQUIRE(r.items.empty());
    REQUIRE(&r.selected() == &c.root);
    REQUIRE(argparse::apply(r));
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
    REQUIRE(argparse::apply(r));
    // callbacks are called from the root to the selected command
    REQUIRE(c.selected == std::vector<std::string>{"image", "ls"});

    // a command with subcommands does not require one to be given
    REQUIRE(c.parse({"image"}).ok());

    auto bad = c.parse({"image", "lx"});
    REQUIRE(kinds(bad) == std::vector{error_kind::unknown_command});
    REQUIRE(bad.errors[0].word == 1u);
    REQUIRE(bad.errors[0].message.find("'lx'") != std::string::npos);
}

TEST_CASE("parse has no side effects", "[argparse]") {
    cli c;
    auto r = c.parse({"-vv", "--repo=/r", "--color", "run", "-V", "--view", "x",
                      "img", "cmd"});
    REQUIRE(r.ok());
    REQUIRE(c.verbose == 0);
    REQUIRE(!c.repo);
    REQUIRE(!c.color);
    REQUIRE(c.uenv.empty());
    REQUIRE(c.commands.empty());
    REQUIRE(!c.no_default_view);
    REQUIRE(c.selected.empty());

    REQUIRE(argparse::apply(r));
    REQUIRE(c.verbose == 2);
    REQUIRE(c.repo == "/r");
    REQUIRE(c.color == true);
    REQUIRE(c.view == "x");
    REQUIRE(c.uenv == "img");
    REQUIRE(c.commands == std::vector<std::string>{"cmd"});
    REQUIRE(c.no_default_view);
    REQUIRE(c.selected == std::vector<std::string>{"run"});
}

TEST_CASE("long options", "[argparse]") {
    SECTION("value in the next word") {
        cli c;
        auto r = c.parse({"--repo", "/a"});
        REQUIRE(r.ok());
        REQUIRE(r.items.size() == 2u);
        REQUIRE(r.items[0].kind == item_kind::option);
        REQUIRE(!r.items[0].value);
        REQUIRE(r.items[1].kind == item_kind::option_value);
        REQUIRE(r.items[1].value == "/a");
        REQUIRE(argparse::apply(r));
        REQUIRE(c.repo == "/a");
    }
    SECTION("value in the same word") {
        cli c;
        auto r = c.parse({"--repo=/a=b"});
        REQUIRE(r.ok());
        REQUIRE(r.items.size() == 1u);
        REQUIRE(r.items[0].value == "/a=b");
        REQUIRE(argparse::apply(r));
        REQUIRE(c.repo == "/a=b");
    }
    SECTION("empty value in the same word") {
        cli c;
        auto r = c.parse({"--repo="});
        REQUIRE(r.ok());
        REQUIRE(argparse::apply(r));
        REQUIRE(c.repo == "");
    }
    SECTION("a value that looks like an option") {
        cli c;
        auto r = c.parse({"--repo", "--color"});
        REQUIRE(r.ok());
        REQUIRE(argparse::apply(r));
        REQUIRE(c.repo == "--color");
        REQUIRE(!c.color);
    }
    SECTION("missing value") {
        cli c;
        auto r = c.parse({"--repo"});
        REQUIRE(kinds(r) == std::vector{error_kind::missing_value});
        REQUIRE(r.pending == c.root.find_long("repo"));
        REQUIRE(!argparse::apply(r));
    }
    SECTION("unknown option") {
        cli c;
        auto r = c.parse({"--bogus=12"});
        REQUIRE(kinds(r) == std::vector{error_kind::unknown_option});
        REQUIRE(r.errors[0].message.find("'--bogus'") != std::string::npos);
        REQUIRE(r.items[0].kind == item_kind::unrecognised);
    }
    SECTION("a flag does not take a value") {
        cli c;
        auto r = c.parse({"image", "ls", "--json=false"});
        REQUIRE(kinds(r) == std::vector{error_kind::flag_with_value});
    }
    SECTION("repeated option") {
        cli c;
        auto r = c.parse({"--repo", "a", "--repo=b"});
        REQUIRE(kinds(r) == std::vector{error_kind::repeated_option});
        REQUIRE(r.errors[0].word == 2u);
    }
}

TEST_CASE("flags", "[argparse]") {
    SECTION("counting") {
        cli c;
        auto r = c.parse({"-v", "--verbose", "-vv"});
        REQUIRE(r.ok());
        REQUIRE(argparse::apply(r));
        REQUIRE(c.verbose == 4);
    }
    SECTION("a counter is set to zero when it is not given") {
        cli c;
        c.verbose = 7;
        auto r = c.parse({});
        REQUIRE(argparse::apply(r));
        REQUIRE(c.verbose == 0);
    }
    SECTION("repeated boolean flags are allowed") {
        cli c;
        auto r = c.parse({"image", "ls", "--json", "--json"});
        REQUIRE(r.ok());
        REQUIRE(argparse::apply(r));
        REQUIRE(c.json);
    }
    SECTION("negation: the last one wins") {
        {
            cli c;
            auto r = c.parse({"repo", "update", "x", "--no-lustre"});
            REQUIRE(r.ok());
            REQUIRE(r.items.back().negated);
            REQUIRE(argparse::apply(r));
            REQUIRE(!c.lustre);
        }
        {
            cli c;
            auto r =
                c.parse({"repo", "update", "--no-lustre", "x", "--lustre"});
            REQUIRE(r.ok());
            REQUIRE(argparse::apply(r));
            REQUIRE(c.lustre);
        }
    }
    SECTION("callbacks are called in the order given") {
        {
            cli c;
            auto r = c.parse({"--color", "--no-color"});
            REQUIRE(r.ok());
            REQUIRE(argparse::apply(r));
            REQUIRE(c.color == false);
        }
        {
            cli c;
            auto r = c.parse({"--no-color", "--color", "--no-color"});
            REQUIRE(r.ok());
            REQUIRE(argparse::apply(r));
            REQUIRE(c.color == false);
        }
        {
            cli c;
            auto r = c.parse({"--no-color", "--color"});
            REQUIRE(argparse::apply(r));
            REQUIRE(c.color == true);
        }
    }
}

TEST_CASE("short options", "[argparse]") {
    SECTION("cluster of flags") {
        cli c;
        auto r = c.parse({"run", "-Vj", "img", "cmd"});
        REQUIRE(r.ok());
        REQUIRE(argparse::apply(r));
        REQUIRE(c.no_default_view);
        REQUIRE(c.join);
    }
    SECTION("attached value") {
        cli c;
        auto r = c.parse({"run", "-Vvdefault", "img", "cmd"});
        REQUIRE(r.ok());
        REQUIRE(argparse::apply(r));
        REQUIRE(c.no_default_view);
        REQUIRE(c.view == "default");
    }
    SECTION("value in the next word") {
        cli c;
        auto r = c.parse({"run", "-jv", "default", "img", "cmd"});
        REQUIRE(r.ok());
        REQUIRE(argparse::apply(r));
        REQUIRE(c.join);
        REQUIRE(c.view == "default");
    }
    SECTION("options are scoped to their command") {
        // -v is --verbose at the root, and --view under run
        cli c;
        auto r = c.parse({"-v", "run", "-v", "x", "img", "cmd"});
        REQUIRE(r.ok());
        REQUIRE(argparse::apply(r));
        REQUIRE(c.verbose == 1);
        REQUIRE(c.view == "x");

        REQUIRE(kinds(c.parse({"image", "ls", "--verbose"})) ==
                std::vector{error_kind::unknown_option});
    }
    SECTION("unknown option in a cluster") {
        cli c;
        auto r = c.parse({"run", "-Vx", "img", "cmd"});
        REQUIRE(kinds(r) == std::vector{error_kind::unknown_option});
        REQUIRE(r.errors[0].message.find("'-x'") != std::string::npos);
    }
    SECTION("a lone dash is a positional argument") {
        cli c;
        auto r = c.parse({"image", "ls", "-"});
        REQUIRE(r.ok());
        REQUIRE(argparse::apply(r));
        REQUIRE(c.ls_uenv == "-");
    }
}

TEST_CASE("positionals", "[argparse]") {
    SECTION("optional positional") {
        cli c;
        auto r = c.parse({"image", "ls"});
        REQUIRE(r.ok());
        REQUIRE(r.next_positional == 0u);
        REQUIRE(argparse::apply(r));
        REQUIRE(!c.ls_uenv);
    }
    SECTION("too many") {
        cli c;
        auto r = c.parse({"image", "ls", "a", "b"});
        REQUIRE(kinds(r) == std::vector{error_kind::unexpected_argument});
        REQUIRE(r.errors[0].word == 3u);
    }
    SECTION("required") {
        cli c;
        REQUIRE(kinds(c.parse({"run"})) ==
                std::vector{error_kind::missing_positional,
                            error_kind::missing_positional});
        REQUIRE(kinds(c.parse({"run", "img"})) ==
                std::vector{error_kind::missing_positional});
    }
    SECTION("rest takes every remaining word") {
        cli c;
        auto r = c.parse({"run", "img", "ls", "-l", "--view", "x", "--"});
        REQUIRE(r.ok());
        REQUIRE(r.in_rest);
        REQUIRE(argparse::apply(r));
        REQUIRE(c.uenv == "img");
        REQUIRE(!c.view);
        REQUIRE(c.commands ==
                std::vector<std::string>{"ls", "-l", "--view", "x", "--"});
    }
    SECTION("end of options") {
        cli c;
        auto r = c.parse({"run", "--", "-img", "--view"});
        REQUIRE(r.ok());
        REQUIRE(r.end_of_options);
        REQUIRE(r.items[1].kind == item_kind::end_of_options);
        REQUIRE(argparse::apply(r));
        REQUIRE(c.uenv == "-img");
        REQUIRE(c.commands == std::vector<std::string>{"--view"});
    }
    SECTION("options between positionals") {
        cli c;
        auto r = c.parse({"run", "img", "-V", "--", "cmd"});
        REQUIRE(r.ok());
        REQUIRE(argparse::apply(r));
        REQUIRE(c.no_default_view);
        REQUIRE(c.commands == std::vector<std::string>{"cmd"});
    }
    SECTION("-- in a command with subcommands") {
        cli c;
        REQUIRE(kinds(c.parse({"image", "--", "ls"})) ==
                std::vector{error_kind::unexpected_argument});
    }
    SECTION("empty words") {
        cli c;
        auto r = c.parse({"run", "", ""});
        REQUIRE(r.ok());
        REQUIRE(argparse::apply(r));
        REQUIRE(c.uenv == "");
        REQUIRE(c.commands == std::vector<std::string>{""});
        REQUIRE(kinds(c.parse({""})) ==
                std::vector{error_kind::unknown_command});
    }
}

TEST_CASE("required options", "[argparse]") {
    cli c;
    REQUIRE(kinds(c.parse({"image", "delete", "x"})) ==
            std::vector{error_kind::missing_option});
    auto r = c.parse({"image", "delete", "--token", "t", "x"});
    REQUIRE(r.ok());
    REQUIRE(argparse::apply(r));
    REQUIRE(c.token == "t");
    REQUIRE(c.delete_uenv == "x");
}

TEST_CASE("choices", "[argparse]") {
    {
        cli c;
        auto r = c.parse({"status", "--format=short"});
        REQUIRE(r.ok());
        REQUIRE(argparse::apply(r));
        REQUIRE(c.format == fmt_choice::name);
    }
    {
        cli c;
        auto r = c.parse({"status", "--format", "views"});
        REQUIRE(r.ok());
        REQUIRE(argparse::apply(r));
        REQUIRE(c.format == fmt_choice::views);
    }
    {
        cli c;
        auto r = c.parse({"status", "--format", "name"});
        REQUIRE(kinds(r) == std::vector{error_kind::invalid_choice});
        REQUIRE(!argparse::apply(r));
        REQUIRE(c.format == fmt_choice::full);
    }
    {
        cli c;
        auto o = c.root.find_subcommand("status")->find_long("format");
        REQUIRE(o->completer().type == completion::kind::choice);
        REQUIRE(o->choices() ==
                std::vector<std::string>{"short", "full", "views"});
    }
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
        auto a = argparse::apply(r);
        REQUIRE(a);
        REQUIRE(a->help == r.help);
    }
    // nothing is applied when help is requested
    auto r = c.parse({"-v", "run", "-h"});
    REQUIRE(argparse::apply(r));
    REQUIRE(c.verbose == 0);
    REQUIRE(c.selected.empty());

    auto text = argparse::render_help(*c.root.find_subcommand("run"));
    REQUIRE(text.find("Usage: prog run [OPTIONS] uenv commands...") !=
            std::string::npos);
    REQUIRE(text.find("-v, --view VIEW") != std::string::npos);
    REQUIRE(text.find("-h, --help") != std::string::npos);
    REQUIRE(text.find("(required)") != std::string::npos);

    auto root = argparse::render_help(c.root);
    REQUIRE(root.find("Usage: prog [OPTIONS] SUBCOMMAND") != std::string::npos);
    REQUIRE(root.find("Subcommands:") != std::string::npos);
    REQUIRE(root.find("--color") != std::string::npos);

    auto update = argparse::render_help(
        *c.root.find_subcommand("repo")->find_subcommand("update"));
    REQUIRE(update.find("--lustre, --no-lustre") != std::string::npos);

    auto status = argparse::render_help(*c.root.find_subcommand("status"));
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

TEST_CASE("validation", "[argparse]") {
    std::string s;
    std::optional<std::string> o;
    std::vector<std::string> v;
    bool b = false;

    auto invalid = [](const argparse::command& c, std::string_view what) {
        auto r = c.validate();
        REQUIRE(!r);
        INFO(r.error());
        REQUIRE(r.error().find(what) != std::string::npos);
    };

    {
        argparse::command c("prog", "");
        c.add_subcommand("a", "");
        c.add_positional("p", s, "").complete(completion::none());
        invalid(c, "both subcommands and positional");
    }
    {
        argparse::command c("prog", "");
        c.add_subcommand("a", "");
        c.add_subcommand("a", "");
        invalid(c, "duplicate subcommand 'a'");
    }
    {
        argparse::command c("prog", "");
        c.add_flag("x", b, "");
        c.add_option("x", o, "").complete(completion::none());
        invalid(c, "duplicate option '--x'");
    }
    {
        argparse::command c("prog", "");
        c.add_flag({'h', "x"}, b, "");
        invalid(c, "duplicate option '-h'");
    }
    {
        argparse::command c("prog", "");
        c.add_flag("help", b, "");
        invalid(c, "duplicate option '--help'");
    }
    {
        argparse::command c("prog", "");
        c.add_flag("x", b, "").negation("help");
        invalid(c, "duplicate option '--help'");
    }
    {
        argparse::command c("prog", "");
        c.add_option("x", o, "").negation("no-x").complete(completion::none());
        invalid(c, "only a boolean flag can have a negation");
    }
    {
        int i = 0;
        argparse::command c("prog", "");
        c.add_flag("x", i, "").negation("no-x");
        invalid(c, "only a boolean flag can have a negation");
    }
    {
        argparse::command c("prog", "");
        c.add_flag("x", [] {}, "").negation("no-x");
        invalid(c, "only a boolean flag can have a negation");
    }
    {
        argparse::command c("prog", "");
        c.add_option("x", o, "");
        invalid(c, "option --x has no completion");
    }
    {
        argparse::command c("prog", "");
        c.add_flag("x", b, "").required();
        invalid(c, "flag --x can't be required");
    }
    {
        argparse::command c("prog", "");
        c.add_flag("", b, "");
        invalid(c, "an option has no name");
    }
    for (auto name : {"-x", "x=y", "x y", "é", "--"}) {
        argparse::command c("prog", "");
        c.add_flag(std::string(name), b, "");
        invalid(c, "invalid option name");
    }
    {
        argparse::command c("prog", "");
        c.add_flag('-', b, "");
        invalid(c, "invalid short option name");
    }
    {
        argparse::command c("prog", "");
        c.add_rest("r", v, "").complete(completion::none());
        c.add_positional("p", s, "").complete(completion::none());
        invalid(c, "must be last");
    }
    {
        argparse::command c("prog", "");
        c.add_positional("p", o, "").complete(completion::none());
        c.add_positional("q", s, "").required().complete(completion::none());
        invalid(c, "follows an optional one");
    }
    {
        argparse::command c("prog", "");
        c.add_positional("p", s, "");
        invalid(c, "positional 'p' has no completion");
    }
    {
        argparse::command c("prog", "");
        c.add_subcommand("a", "").add_subcommand("-b", "");
        invalid(c, "prog a -b: invalid command name");
    }
}
