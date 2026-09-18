#pragma once

// A small command line argument parser.
//
// The command line interface is described as a tree of `command`s, each with
// its own options (flags and options that take a value), positional arguments
// and subcommands. The application builds each command as a value, filling it
// in with `add_flag`, `add_option`, ..., and adds it to its parent with
// `add_subcommand`:
//
//   argparse::command ls_cli() {
//       argparse::command ls("ls", "list images");
//       ls.add_flag("json", json, "print JSON");
//       return ls;
//   }
//   ...
//   image.add_subcommand(ls_cli());
//
// Parsing is split into two steps:
//
// 1. `parse()` classifies every word on the command line against the tree, and
//    returns a `parse_result`: a trace recording which word went to which
//    subcommand, option or positional argument, the state of the parser after
//    the last word, and a list of errors. `parse()` has no side effects: it
//    never writes to the variables bound to options and never calls callbacks.
//    It does not stop at the first error, so it can be used on an incomplete
//    command line (e.g. for tab completion) as well as on a complete one.
//
// 2. `apply()` takes an error-free `parse_result`, writes the values to the
//    bound variables and calls the `on_selected` callback of each command on
//    the path from the root to the selected subcommand.
//
// Grammar of a single word, applied in this order:
//
//   - while an option is waiting for its value, the word is that value, even
//     if it starts with a dash (`--view -x` sets view to "-x");
//   - after `--`, or once a `rest` positional has received its first word,
//     every word is a positional argument;
//   - `--` ends option processing;
//   - `--name` is a long option or flag; `--name=value` gives the value of an
//     option in the same word;
//   - `-abc` is a cluster of short flags; if a short option in a cluster takes
//     a value, the remainder of the word is its value (`-vfoo`, `-avfoo`), or
//     if nothing remains, the next word is;
//   - `-` on its own is a positional argument;
//   - any other word is a subcommand name (if the command has subcommands) or
//     the next positional argument (if it has positionals). A command can have
//     one or the other, not both.
//
// Options only apply to the command whose words they appear among: `uenv -v`
// and `uenv run -v` can refer to different options.
//
// Each command automatically gets a `-h,--help` flag. When it is given, the
// errors that would otherwise be reported (e.g. a missing required argument)
// are ignored, and `apply()` reports that help was requested instead of
// applying the values.
//
// This library depends only on src/util and fmt: it must not include anything
// from src/uenv, src/cli or src/site.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <util/expected.h>

namespace argparse {

class command;
// grants apply() access to the callbacks stored in the tree
struct access;

// A description of how the value of an option or positional argument can be
// completed interactively. Every argument that takes a value must set one
// (see command::validate). The library does not interpret `custom` tags: they
// are resolved by the application.
struct completion {
    enum class kind : std::uint8_t {
        unset,     // not set: rejected by command::validate
        none,      // free text, nothing to offer
        file,      // a file matching the glob in `arg`
        directory, // a directory
        path,      // any path
        choice,    // one of a fixed set of values (set by add_choice)
        command,   // a command and its arguments, completed by the shell
        custom,    // application-defined, identified by the tag in `arg`
    };
    kind type = kind::unset;
    std::string arg;

    static completion none();
    static completion file(std::string glob = "*");
    static completion directory();
    static completion path();
    static completion command();
    static completion custom(std::string tag);
};

// The names of an option: a single character short name (`-v`) and/or a long
// name (`--view`), given without their leading dashes.
struct names {
    names(char short_name, std::string long_name);
    names(std::string long_name);
    names(const char* long_name);
    names(char short_name);

    std::optional<char> short_name;
    std::string long_name;
};

// One occurrence of an option on the command line.
struct occurrence {
    // true if the flag was given using its negation (e.g. --no-lustre)
    bool negated = false;
    // the value of an option that takes a value
    std::string_view value;
};

// A flag (takes no value) or an option (takes exactly one value).
class option {
  public:
    // the option must be given (only options that take a value).
    option& required(bool r = true);
    // add a negating long name to a boolean flag, e.g. --no-lustre.
    option& negation(std::string long_name);
    // how the value can be completed (only options that take a value).
    option& complete(completion c);
    // the placeholder used for the value in help output.
    option& metavar(std::string m);

    std::optional<char> short_name() const {
        return names_.short_name;
    }
    const std::string& long_name() const {
        return names_.long_name;
    }
    const std::optional<std::string>& negation_name() const {
        return negation_;
    }
    const std::string& help() const {
        return help_;
    }
    bool takes_value() const {
        return type_ == type::value || type_ == type::choice;
    }
    bool is_required() const {
        return required_;
    }
    bool is_help() const {
        return type_ == type::help;
    }
    // only a boolean flag can have a negation
    bool negatable() const {
        return type_ == type::boolean;
    }
    const struct completion& completer() const {
        return completion_;
    }
    // the accepted values of an option created with add_choice
    const std::vector<std::string>& choices() const {
        return choices_;
    }
    std::string metavar() const;
    // the name used in messages: --long if there is one, else -s
    std::string display_name() const;

  private:
    friend class command;

    // the kind of option, which determines how occurrences are applied
    enum class type : std::uint8_t {
        boolean,  // bool flag: the last occurrence wins
        counter,  // int flag: counts occurrences
        callback, // flag: callback called each time the flag is given
        value,    // takes a value, may be given at most once
        choice,   // takes a value from a fixed set, may be given at most once
        help,     // the built-in -h,--help flag
    };

    option(names n, type t, std::string help);

    names names_;
    type type_;
    std::string help_;
    std::optional<std::string> negation_;
    bool required_ = false;
    struct completion completion_;
    std::string metavar_;
    std::vector<std::string> choices_;
    // called by apply() with every occurrence of the option, if it was given
    std::function<void(std::span<const occurrence>)> apply_;

    friend struct access;
};

// A positional argument: either a single word, or (`rest`) all remaining
// words on the command line.
class positional {
  public:
    positional& required(bool r = true);
    positional& complete(completion c);

    const std::string& name() const {
        return name_;
    }
    const std::string& help() const {
        return help_;
    }
    bool is_required() const {
        return required_;
    }
    bool is_rest() const {
        return rest_;
    }
    const struct completion& completer() const {
        return completion_;
    }

  private:
    friend class command;

    positional(std::string name, bool rest, std::string help);

    std::string name_;
    std::string help_;
    bool rest_;
    bool required_ = false;
    struct completion completion_;
    std::function<void(std::span<const std::string_view>)> apply_;

    friend struct access;
};

class command {
  public:
    command(std::string name, std::string description);

    command(const command&) = delete;
    command& operator=(const command&) = delete;
    // moving a command keeps the parent links of its subcommands valid
    command(command&&);
    command& operator=(command&&);

    //
    // building the tree
    //

    // add a subcommand, returning a reference to it in the tree
    command& add_subcommand(command sub);

    // a boolean flag: true if given (or false if given by its negation)
    option& add_flag(names n, bool& target, std::string help);
    // a counting flag: the number of times it was given, e.g. -vvv -> 3. The
    // target is always set by apply(), to zero if the flag was not given.
    option& add_flag(names n, int& target, std::string help);
    // a flag that calls `callback` each time it is given, in the order of
    // the command line
    option& add_flag(names n, std::function<void()> callback, std::string help);

    option& add_option(names n, std::string& target, std::string help);
    option& add_option(names n, std::optional<std::string>& target,
                       std::string help);

    // an option whose value must be one of the keys of `values`
    template <typename T>
    option& add_choice(names n, T& target,
                       std::vector<std::pair<std::string, T>> values,
                       std::string help) {
        std::vector<std::string> keys;
        for (auto& v : values) {
            keys.push_back(v.first);
        }
        auto setter = [&target,
                       values = std::move(values)](std::string_view value) {
            for (auto& v : values) {
                if (v.first == value) {
                    target = v.second;
                    return;
                }
            }
        };
        return add_choice_impl(std::move(n), std::move(keys), std::move(setter),
                               std::move(help));
    }

    positional& add_positional(std::string name, std::string& target,
                               std::string help);
    positional& add_positional(std::string name,
                               std::optional<std::string>& target,
                               std::string help);
    // a positional that takes every remaining word on the command line. It
    // must be the last positional.
    positional& add_rest(std::string name, std::vector<std::string>& target,
                         std::string help);
    positional& add_rest(std::string name,
                         std::optional<std::vector<std::string>>& target,
                         std::string help);

    // text printed after the generated help, generated when help is printed
    command& footer(std::function<std::string()> f);
    // called by apply() if this command is on the path of selected commands
    command& on_selected(std::function<void()> f);

    //
    // inspecting the tree
    //

    const std::string& name() const {
        return name_;
    }
    const std::string& description() const {
        return description_;
    }
    const command* parent() const {
        return parent_;
    }
    std::string footer_text() const;
    // the names of the commands from the root to this command
    std::vector<std::string> path() const;

    std::vector<const option*> options() const;
    std::vector<const positional*> positionals() const;
    std::vector<const command*> subcommands() const;

    const option* find_long(std::string_view name) const;
    const option* find_short(char name) const;
    const command* find_subcommand(std::string_view name) const;

    // check that the tree rooted at this command is well formed: names are
    // valid and unique, a command does not have both subcommands and
    // positionals, a rest positional is last, a required positional does not
    // follow an optional one, and every argument that takes a value has a
    // completion.
    util::expected<void, std::string> validate() const;

  private:
    friend struct access;

    void adopt_subcommands();
    option& add(std::unique_ptr<option> o);
    positional& add(std::unique_ptr<positional> p);
    option& add_choice_impl(names n, std::vector<std::string> keys,
                            std::function<void(std::string_view)> setter,
                            std::string help);

    std::string name_;
    std::string description_;
    command* parent_ = nullptr;
    std::function<std::string()> footer_;
    std::function<void()> on_selected_;
    std::vector<std::unique_ptr<option>> options_;
    std::vector<std::unique_ptr<positional>> positionals_;
    std::vector<std::unique_ptr<command>> subcommands_;
};

//
// parsing
//

// what a word, or part of a word, on the command line was recognised as
enum class item_kind : std::uint8_t {
    subcommand,     // selected a subcommand
    flag,           // a flag
    option,         // an option name; its value is in `value` if it was given
                    // in the same word (--name=value, -nvalue)
    option_value,   // the value of the preceding option, in its own word
    positional,     // a positional argument
    end_of_options, // --
    unrecognised,   // could not be placed: see the errors
};

struct item {
    // index of the word in the input
    std::size_t word;
    item_kind kind;
    // the command whose words this item was parsed among
    const command* cmd;
    const option* opt = nullptr;
    const positional* pos = nullptr;
    // for a flag: given by its negation
    bool negated = false;
    // the value of an option or positional
    std::optional<std::string_view> value;
};

enum class error_kind : std::uint8_t {
    unknown_option,      // --foo or -f is not an option of the command
    unknown_command,     // the command has subcommands, none by this name
    unexpected_argument, // more positional arguments than the command takes
    missing_value,       // an option at the end of the line has no value
    flag_with_value,     // --flag=value
    invalid_choice,      // the value is not one of the accepted choices
    repeated_option,     // an option that takes a value was given twice
    missing_option,      // a required option was not given
    missing_positional,  // a required positional argument was not given
};

struct error {
    error_kind kind;
    // the command in which the error occurred
    const command* cmd;
    // the word that caused the error, if there is one
    std::optional<std::size_t> word;
    std::string message;
};

struct parse_result {
    // one or more items per word, in the order of the words
    std::vector<item> items;
    std::vector<error> errors;
    // the commands selected, from the root to the most deeply nested
    std::vector<const command*> path;

    //
    // the state after the last word
    //

    // an option that is still waiting for its value
    const option* pending = nullptr;
    // index of the positional argument that the next word would fill in the
    // selected command (equal to the number of positionals when all are full)
    std::size_t next_positional = 0;
    // `--` has been seen in the selected command
    bool end_of_options = false;
    // a rest positional has started: every following word is its argument
    bool in_rest = false;
    // the command for which -h/--help was given, if any
    const command* help = nullptr;

    const command& selected() const {
        return *path.back();
    }
    // no errors, or help was requested (which takes precedence over errors)
    bool ok() const {
        return errors.empty() || help != nullptr;
    }
};

// classify every word in `words` (which should not include the program name)
// against the tree rooted at `root`.
parse_result parse(const command& root,
                   std::span<const std::string_view> words);
parse_result parse(const command& root, std::span<const std::string> words);
// parse argv[1..argc)
parse_result parse(const command& root, int argc, const char* const* argv);

// What apply() did.
struct applied {
    // help was requested for this command: nothing was applied.
    const command* help = nullptr;
};

// Write the parsed values to the bound variables and call the on_selected
// callbacks. The result must be ok(): if it has errors (and help was not
// requested) nothing is applied and the first error is returned.
util::expected<applied, error> apply(const parse_result& result);

// The help text for a command: usage, positionals, options, subcommands and
// the footer.
std::string render_help(const command& cmd);

} // namespace argparse
