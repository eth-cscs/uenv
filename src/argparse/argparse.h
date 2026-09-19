#pragma once

// A small command line argument parser.
//
// The command line interface is described as a tree of `command`s, each with
// its own options (flags and options that take a value), positional arguments
// and subcommands.
//
// Each command has an arguments type: a plain struct whose fields hold the
// values given on the command line. A command is built with a
// `command_builder<T>`, which binds each option and positional argument to a
// field of `T` by pointer-to-member, and sets the command's action: what the
// command does with those values. Subcommands are built as values and added to
// their parent with `add_subcommand`:
//
//   struct ls_args {
//       bool json = false;
//   };
//
//   argparse::command ls_command() {
//       argparse::command_builder<ls_args> ls("ls", "list images");
//       ls.add_flag("json", &ls_args::json, "print JSON");
//       ls.action([](const ls_args& args) { return list(args); });
//       return std::move(ls).build();
//   }
//   ...
//   image.add_subcommand(ls_command());
//
// The root of the tree is wrapped in a `program<Globals>`, where `Globals` is
// the arguments type of the root: the program's global options.
//
//   argparse::program<global_args> cli(std::move(root));
//   auto invocation = cli.parse(argc, argv);   // or an error
//   invocation->globals();                     // the global options
//   invocation->run();                         // the selected command's action
//
// Parsing is split into two steps:
//
// 1. `parse()` classifies every word on the command line against the tree, and
//    returns a `parse_result`: a trace recording which word went to which
//    subcommand, option or positional argument, the state of the parser after
//    the last word, and a list of errors. It does not stop at the first error,
//    so it can be used on an incomplete command line (e.g. for tab completion)
//    as well as on a complete one. It only reads the tree.
//
// 2. `program::parse()` also turns an error-free result into an `invocation`:
//    new values of the root's and the selected command's arguments types,
//    copied from their defaults and filled in from the command line. The
//    selected command's values are bound to its action, and the root's are
//    the global options.
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
// are ignored, and the invocation reports that help was requested.
//
// This library depends only on src/util and fmt: it must not include anything
// from src/uenv, src/cli or src/site.

#include <concepts>
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
template <typename T> class command_builder;
struct parse_result;

// The requirements on the arguments type of a command: a fresh value is made
// for every parse by copying the defaults.
template <typename T>
concept Arguments = std::default_initializable<T> && std::copy_constructible<T>;

// The arguments type of a command that has no options of its own, e.g. one
// that only groups subcommands.
struct no_args {};

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

// A flag (takes no value) or an option (takes exactly one value). This is the
// syntax of the option: the field of the arguments type that it sets is known
// only to the command's builder.
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
    // a counting flag, which is meant to be given more than once
    bool repeatable() const {
        return type_ == type::counter;
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
    template <typename T> friend class command_builder;

    enum class type : std::uint8_t {
        boolean, // flag: true, or false if given by its negation; last wins
        counter, // flag: the number of times it was given
        value,   // takes a value, may be given at most once
        choice,  // takes a value from a fixed set, may be given at most once
        help,    // the built-in -h,--help flag
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
    template <typename T> friend class command_builder;

    positional(std::string name, bool rest, std::string help);

    std::string name_;
    std::string help_;
    bool rest_;
    bool required_ = false;
    struct completion completion_;
};

namespace detail {

// The occurrences of the options and positional arguments of one command on
// the command line, in the order they were given.
struct gathered {
    std::vector<std::pair<const option*, std::vector<occurrence>>> options;
    std::vector<std::pair<const positional*, std::vector<std::string_view>>>
        positionals;
};

// What a command does with its occurrences: make the command's action, bound
// to a value of its arguments type filled in from the occurrences. The typed
// implementation, model<T>, is made by command_builder<T>.
struct model_interface {
    virtual ~model_interface() = default;
    // an empty function if the command has no action
    virtual std::function<int()> make(const gathered& occurrences,
                                      const command& cmd) const = 0;
};

// The occurrences of the options and positional arguments of `cmd` in a
// result. Words that could not be placed (see the errors) are left out.
gathered gather(const parse_result& result, const command& cmd);

} // namespace detail

class command {
  public:
    command(const command&) = delete;
    command& operator=(const command&) = delete;
    // moving a command keeps the parent links of its subcommands valid
    command(command&&);
    command& operator=(command&&);
    ~command();

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
    template <typename T> friend class command_builder;
    template <Arguments G> friend class program;

    command(std::string name, std::string description,
            std::unique_ptr<detail::model_interface> model);

    void adopt_subcommands();
    option& add(std::unique_ptr<option> o);
    positional& add(std::unique_ptr<positional> p);
    command& add_subcommand(command sub);

    std::string name_;
    std::string description_;
    command* parent_ = nullptr;
    std::function<std::string()> footer_;
    std::unique_ptr<detail::model_interface> model_;
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
// against the tree rooted at `root`. The result views the words, so they must
// outlive it.
parse_result parse(const command& root,
                   std::span<const std::string_view> words);
parse_result parse(const command& root, std::span<const std::string> words);

// The help text for a command: usage, positionals, options, subcommands and
// the footer.
std::string render_help(const command& cmd);

//
// building commands
//

namespace detail {

template <Arguments T> struct model final : model_interface {
    using option_setter = std::function<void(T&, std::span<const occurrence>)>;
    using positional_setter =
        std::function<void(T&, std::span<const std::string_view>)>;

    T defaults;
    std::vector<std::pair<const option*, option_setter>> option_setters;
    std::vector<std::pair<const positional*, positional_setter>>
        positional_setters;
    std::function<int(const T&, const command&)> action;

    // the defaults, with the occurrences applied
    T fill(const gathered& g) const {
        T values = defaults;
        for (auto& [opt, occ] : g.options) {
            for (auto& [o, set] : option_setters) {
                if (o == opt) {
                    set(values, occ);
                }
            }
        }
        for (auto& [pos, words] : g.positionals) {
            for (auto& [p, set] : positional_setters) {
                if (p == pos) {
                    set(values, words);
                }
            }
        }
        return values;
    }

    std::function<int()> make(const gathered& g,
                              const command& cmd) const override {
        if (!action) {
            return {};
        }
        // the action owns a copy of the values
        return [values = fill(g), action = action, &cmd] {
            return action(values, cmd);
        };
    }
};

} // namespace detail

// Builds a command whose arguments type is T: every option and positional
// argument sets a field of T, and the action receives the filled-in value.
template <typename T = no_args> class command_builder {
    static_assert(Arguments<T>);

  public:
    // `defaults` is the value of the arguments before the command line is
    // applied: the value of every field whose option is not given.
    command_builder(std::string name, std::string description, T defaults = T{})
        : cmd_(std::move(name), std::move(description),
               std::make_unique<detail::model<T>>()),
          model_(static_cast<detail::model<T>*>(cmd_.model_.get())) {
        model_->defaults = std::move(defaults);
    }

    command_builder(command_builder&&) = default;
    command_builder& operator=(command_builder&&) = default;

    // a boolean flag: true if given, false if given by its negation (see
    // option::negation), the last one given wins
    option& add_flag(names n, bool T::* field, std::string help) {
        return add_option_impl(std::move(n), option::type::boolean,
                               std::move(help),
                               [field](T& v, std::span<const occurrence> occ) {
                                   v.*field = !occ.back().negated;
                               });
    }
    // a boolean flag that is unset if it is not given
    option& add_flag(names n, std::optional<bool> T::* field,
                     std::string help) {
        return add_option_impl(std::move(n), option::type::boolean,
                               std::move(help),
                               [field](T& v, std::span<const occurrence> occ) {
                                   v.*field = !occ.back().negated;
                               });
    }
    // a counting flag: the number of times it was given, e.g. -vvv -> 3
    option& add_flag(names n, int T::* field, std::string help) {
        return add_option_impl(std::move(n), option::type::counter,
                               std::move(help),
                               [field](T& v, std::span<const occurrence> occ) {
                                   v.*field = static_cast<int>(occ.size());
                               });
    }

    option& add_option(names n, std::string T::* field, std::string help) {
        return add_option_impl(std::move(n), option::type::value,
                               std::move(help),
                               [field](T& v, std::span<const occurrence> occ) {
                                   v.*field = std::string(occ.back().value);
                               });
    }
    option& add_option(names n, std::optional<std::string> T::* field,
                       std::string help) {
        return add_option_impl(std::move(n), option::type::value,
                               std::move(help),
                               [field](T& v, std::span<const occurrence> occ) {
                                   v.*field = std::string(occ.back().value);
                               });
    }

    // an option whose value must be one of the keys of `values`
    template <typename V>
    option& add_choice(names n, V T::* field,
                       std::vector<std::pair<std::string, V>> values,
                       std::string help) {
        std::vector<std::string> keys;
        for (auto& kv : values) {
            keys.push_back(kv.first);
        }
        auto& o =
            add_option_impl(std::move(n), option::type::choice, std::move(help),
                            [field, values = std::move(values)](
                                T& v, std::span<const occurrence> occ) {
                                for (auto& kv : values) {
                                    if (kv.first == occ.back().value) {
                                        v.*field = kv.second;
                                    }
                                }
                            });
        o.choices_ = std::move(keys);
        o.completion_ = {completion::kind::choice, {}};
        return o;
    }

    positional& add_positional(std::string name, std::string T::* field,
                               std::string help) {
        return add_positional_impl(
            std::move(name), false, std::move(help),
            [field](T& v, std::span<const std::string_view> w) {
                v.*field = std::string(w.front());
            });
    }
    positional& add_positional(std::string name,
                               std::optional<std::string> T::* field,
                               std::string help) {
        return add_positional_impl(
            std::move(name), false, std::move(help),
            [field](T& v, std::span<const std::string_view> w) {
                v.*field = std::string(w.front());
            });
    }
    // a positional that takes every remaining word on the command line. It
    // must be the last positional.
    positional& add_rest(std::string name, std::vector<std::string> T::* field,
                         std::string help) {
        return add_positional_impl(
            std::move(name), true, std::move(help),
            [field](T& v, std::span<const std::string_view> w) {
                (v.*field).assign(w.begin(), w.end());
            });
    }
    positional& add_rest(std::string name,
                         std::optional<std::vector<std::string>> T::* field,
                         std::string help) {
        return add_positional_impl(
            std::move(name), true, std::move(help),
            [field](T& v, std::span<const std::string_view> w) {
                v.*field = std::vector<std::string>(w.begin(), w.end());
            });
    }

    // add a subcommand, returning a reference to it in the tree
    command& add_subcommand(command sub) {
        return cmd_.add_subcommand(std::move(sub));
    }

    // text printed after the generated help, generated when help is printed
    command_builder& footer(std::function<std::string()> f) {
        cmd_.footer_ = std::move(f);
        return *this;
    }

    // what the command does when it is selected: called with the parsed
    // values, it returns the exit code. A command that only groups
    // subcommands usually has no action.
    command_builder& action(std::function<int(const T&)> f) {
        model_->action = [f = std::move(f)](const T& v, const command&) {
            return f(v);
        };
        return *this;
    }
    // an action that is also given the command it belongs to, e.g. to walk
    // the tree it is part of
    command_builder& action(std::function<int(const T&, const command&)> f) {
        model_->action = std::move(f);
        return *this;
    }

    command build() && {
        return std::move(cmd_);
    }

  private:
    template <Arguments G> friend class program;

    template <typename F>
    option& add_option_impl(names n, option::type t, std::string help,
                            F setter) {
        auto& o = cmd_.add(std::unique_ptr<option>(
            new option(std::move(n), t, std::move(help))));
        model_->option_setters.emplace_back(&o, std::move(setter));
        return o;
    }

    template <typename F>
    positional& add_positional_impl(std::string name, bool rest,
                                    std::string help, F setter) {
        auto& p = cmd_.add(std::unique_ptr<positional>(
            new positional(std::move(name), rest, std::move(help))));
        model_->positional_setters.emplace_back(&p, std::move(setter));
        return p;
    }

    command cmd_;
    // cmd_'s model: it is on the heap, so the pointer stays valid when the
    // builder, or the command it builds, is moved
    detail::model<T>* model_;
};

//
// programs
//

template <Arguments Globals> class program;

// A valid command line, parsed: the values of the global options, and the
// selected command's action bound to its values. It refers to the program's
// tree, so it must not outlive the program.
template <Arguments Globals> class invocation {
  public:
    // -h/--help was given
    bool help_requested() const {
        return help_ != nullptr;
    }
    // the help of the command that -h/--help was given to, otherwise of the
    // selected command
    std::string help() const {
        return render_help(help_ ? *help_ : *selected_);
    }

    // the values of the root command's options: the global options
    const Globals& globals() const {
        return globals_;
    }

    // the command selected on the command line
    const command& selected() const {
        return *selected_;
    }
    bool has_action() const {
        return bool(action_);
    }
    // run the selected command's action: has_action() must be true
    int run() const {
        return action_();
    }

  private:
    friend class program<Globals>;
    invocation(Globals globals, std::function<int()> action,
               const command* selected, const command* help)
        : globals_(std::move(globals)), action_(std::move(action)),
          selected_(selected), help_(help) {
    }

    Globals globals_;
    std::function<int()> action_;
    const command* selected_;
    const command* help_;
};

// A command line interface: the tree of commands, whose root has the
// arguments type Globals.
template <Arguments Globals> class program {
  public:
    explicit program(command_builder<Globals> root)
        : globals_model_(root.model_),
          root_(std::make_unique<command>(std::move(root).build())) {
    }

    const command& root() const {
        return *root_;
    }
    util::expected<void, std::string> validate() const {
        return root_->validate();
    }

    // parse a command line (without the program name): the first error if it
    // is not valid
    util::expected<invocation<Globals>, error>
    parse(std::span<const std::string_view> words) const {
        auto result = argparse::parse(*root_, words);
        if (!result.ok()) {
            return util::unexpected(std::move(result.errors.front()));
        }
        const command& selected = result.selected();
        return invocation<Globals>(
            globals_model_->fill(detail::gather(result, *root_)),
            selected.model_->make(detail::gather(result, selected), selected),
            &selected, result.help);
    }
    // the values of the global options in a result, which can have errors
    // (e.g. the parse of an incomplete command line): the options that were
    // given are applied to the defaults.
    Globals globals(const parse_result& result) const {
        return globals_model_->fill(detail::gather(result, *root_));
    }
    // parse argv[1..argc)
    util::expected<invocation<Globals>, error>
    parse(int argc, const char* const* argv) const {
        std::vector<std::string_view> words;
        for (int i = 1; i < argc; ++i) {
            words.emplace_back(argv[i]);
        }
        return parse(std::span<const std::string_view>(words));
    }

  private:
    // the root's model, owned by the root: the typed values of the root's
    // options are the global options
    const detail::model<Globals>* globals_model_;
    // on the heap, so that invocations can refer to the tree when the
    // program is moved
    std::unique_ptr<command> root_;
};

} // namespace argparse
