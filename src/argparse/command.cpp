#include <set>
#include <string>
#include <string_view>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <argparse/argparse.h>

namespace argparse {

//
// completion
//

completion completion::none() {
    return {kind::none, {}};
}
completion completion::file(std::string glob) {
    return {kind::file, std::move(glob)};
}
completion completion::directory() {
    return {kind::directory, {}};
}
completion completion::path() {
    return {kind::path, {}};
}
completion completion::command() {
    return {kind::command, {}};
}
completion completion::custom(std::string tag) {
    return {kind::custom, std::move(tag)};
}

//
// names
//

names::names(char s, std::string l) : short_name(s), long_name(std::move(l)) {
}
names::names(std::string l) : long_name(std::move(l)) {
}
names::names(const char* l) : long_name(l) {
}
names::names(char s) : short_name(s) {
}

//
// option
//

option::option(names n, type t, std::string help)
    : names_(std::move(n)), type_(t), help_(std::move(help)) {
}

option& option::required(bool r) {
    required_ = r;
    return *this;
}

option& option::negation(std::string long_name) {
    negation_ = std::move(long_name);
    return *this;
}

option& option::complete(struct completion c) {
    completion_ = std::move(c);
    return *this;
}

option& option::metavar(std::string m) {
    metavar_ = std::move(m);
    return *this;
}

std::string option::metavar() const {
    if (!metavar_.empty()) {
        return metavar_;
    }
    if (type_ == type::choice) {
        return fmt::format("{{{}}}", fmt::join(choices_, ","));
    }
    std::string m;
    for (char c : names_.long_name) {
        m +=
            (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : (c == '-' ? '_' : c);
    }
    return m.empty() ? "VALUE" : m;
}

std::string option::display_name() const {
    if (!names_.long_name.empty()) {
        return "--" + names_.long_name;
    }
    return std::string{'-', *names_.short_name};
}

//
// positional
//

positional::positional(std::string name, bool rest, std::string help)
    : name_(std::move(name)), help_(std::move(help)), rest_(rest) {
}

positional& positional::required(bool r) {
    required_ = r;
    return *this;
}

positional& positional::complete(struct completion c) {
    completion_ = std::move(c);
    return *this;
}

//
// command
//

command::command(std::string name, std::string description,
                 std::unique_ptr<detail::model_interface> model)
    : name_(std::move(name)), description_(std::move(description)),
      model_(std::move(model)) {
    add(std::unique_ptr<option>(
        new option({'h', "help"}, option::type::help,
                   "print this help message and exit")));
}

command::command(command&& other)
    : name_(std::move(other.name_)),
      description_(std::move(other.description_)), parent_(other.parent_),
      footer_(std::move(other.footer_)), model_(std::move(other.model_)),
      options_(std::move(other.options_)),
      positionals_(std::move(other.positionals_)),
      subcommands_(std::move(other.subcommands_)) {
    adopt_subcommands();
}

command& command::operator=(command&& other) {
    name_ = std::move(other.name_);
    description_ = std::move(other.description_);
    parent_ = other.parent_;
    footer_ = std::move(other.footer_);
    model_ = std::move(other.model_);
    options_ = std::move(other.options_);
    positionals_ = std::move(other.positionals_);
    subcommands_ = std::move(other.subcommands_);
    adopt_subcommands();
    return *this;
}

command::~command() = default;

// the subcommands are held by pointer, so only their links back to this
// command have to be updated when it moves
void command::adopt_subcommands() {
    for (auto& sub : subcommands_) {
        sub->parent_ = this;
    }
}

command& command::add_subcommand(command sub) {
    auto& added =
        *subcommands_.emplace_back(std::make_unique<command>(std::move(sub)));
    added.parent_ = this;
    return added;
}

option& command::add(std::unique_ptr<option> o) {
    return *options_.emplace_back(std::move(o));
}

positional& command::add(std::unique_ptr<positional> p) {
    return *positionals_.emplace_back(std::move(p));
}

std::string command::footer_text() const {
    return footer_ ? footer_() : std::string{};
}

std::vector<std::string> command::path() const {
    std::vector<std::string> p;
    for (auto c = this; c != nullptr; c = c->parent_) {
        p.insert(p.begin(), c->name_);
    }
    return p;
}

std::vector<const option*> command::options() const {
    std::vector<const option*> v;
    for (auto& o : options_) {
        v.push_back(o.get());
    }
    return v;
}

std::vector<const positional*> command::positionals() const {
    std::vector<const positional*> v;
    for (auto& p : positionals_) {
        v.push_back(p.get());
    }
    return v;
}

std::vector<const command*> command::subcommands() const {
    std::vector<const command*> v;
    for (auto& c : subcommands_) {
        v.push_back(c.get());
    }
    return v;
}

const option* command::find_long(std::string_view name) const {
    for (auto& o : options_) {
        if (!o->long_name().empty() && o->long_name() == name) {
            return o.get();
        }
        if (o->negation_name() && *o->negation_name() == name) {
            return o.get();
        }
    }
    return nullptr;
}

const option* command::find_short(char name) const {
    for (auto& o : options_) {
        if (o->short_name() && *o->short_name() == name) {
            return o.get();
        }
    }
    return nullptr;
}

const command* command::find_subcommand(std::string_view name) const {
    for (auto& c : subcommands_) {
        if (c->name_ == name) {
            return c.get();
        }
    }
    return nullptr;
}

//
// validation of the tree
//

namespace {

bool is_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

// long option names: an alphanumeric character followed by alphanumeric
// characters, '-' or '_'.
bool valid_long_name(std::string_view n) {
    if (n.empty() || !is_alnum(n.front())) {
        return false;
    }
    for (char c : n) {
        if (!is_alnum(c) && c != '-' && c != '_') {
            return false;
        }
    }
    return true;
}

// command and positional names follow the same rules as long option names.
bool valid_name(std::string_view n) {
    return valid_long_name(n);
}

util::expected<void, std::string> validate_command(const command& cmd) {
    const auto where = fmt::format("{}", fmt::join(cmd.path(), " "));
    auto fail = [&where](std::string msg) {
        return util::unexpected(fmt::format("{}: {}", where, msg));
    };

    if (!valid_name(cmd.name())) {
        return fail(fmt::format("invalid command name '{}'", cmd.name()));
    }
    if (!cmd.subcommands().empty() && !cmd.positionals().empty()) {
        return fail("a command can't have both subcommands and positional "
                    "arguments");
    }

    // option names: unique within the command
    std::set<std::string> long_names;
    std::set<char> short_names;
    for (auto o : cmd.options()) {
        if (o->long_name().empty() && !o->short_name()) {
            return fail("an option has no name");
        }
        if (!o->long_name().empty()) {
            if (!valid_long_name(o->long_name())) {
                return fail(
                    fmt::format("invalid option name '--{}'", o->long_name()));
            }
            if (!long_names.insert(o->long_name()).second) {
                return fail(
                    fmt::format("duplicate option '--{}'", o->long_name()));
            }
        }
        if (auto n = o->negation_name()) {
            if (!o->negatable()) {
                return fail(fmt::format(
                    "only a boolean flag can have a negation ('--{}')", *n));
            }
            if (!valid_long_name(*n)) {
                return fail(fmt::format("invalid option name '--{}'", *n));
            }
            if (!long_names.insert(*n).second) {
                return fail(fmt::format("duplicate option '--{}'", *n));
            }
        }
        if (auto s = o->short_name()) {
            if (!is_alnum(*s)) {
                return fail(fmt::format("invalid short option name '-{}'", *s));
            }
            if (!short_names.insert(*s).second) {
                return fail(fmt::format("duplicate option '-{}'", *s));
            }
        }
        if (o->takes_value()) {
            if (o->completer().type == completion::kind::unset) {
                return fail(fmt::format("option {} has no completion",
                                        o->display_name()));
            }
            if (o->choices().empty() &&
                o->completer().type == completion::kind::choice) {
                return fail(
                    fmt::format("option {} has no choices", o->display_name()));
            }
        } else if (o->is_required()) {
            return fail(
                fmt::format("flag {} can't be required", o->display_name()));
        }
    }

    // positionals: a rest positional is last, and required positionals come
    // before optional ones
    std::set<std::string> positional_names;
    bool seen_optional = false;
    const auto positionals = cmd.positionals();
    for (std::size_t i = 0; i < positionals.size(); ++i) {
        auto p = positionals[i];
        if (!valid_name(p->name())) {
            return fail(fmt::format("invalid positional name '{}'", p->name()));
        }
        if (!positional_names.insert(p->name()).second) {
            return fail(fmt::format("duplicate positional '{}'", p->name()));
        }
        if (p->is_rest() && i + 1 != positionals.size()) {
            return fail(fmt::format("positional '{}' takes the remaining "
                                    "arguments, so it must be last",
                                    p->name()));
        }
        if (p->is_required() && seen_optional) {
            return fail(fmt::format("required positional '{}' follows an "
                                    "optional one",
                                    p->name()));
        }
        seen_optional = seen_optional || !p->is_required();
        if (p->completer().type == completion::kind::unset) {
            return fail(
                fmt::format("positional '{}' has no completion", p->name()));
        }
    }

    // subcommands: unique names, and recursively valid
    std::set<std::string> command_names;
    for (auto c : cmd.subcommands()) {
        if (!command_names.insert(c->name()).second) {
            return fail(fmt::format("duplicate subcommand '{}'", c->name()));
        }
        if (auto r = validate_command(*c); !r) {
            return r;
        }
    }

    return {};
}

} // namespace

util::expected<void, std::string> command::validate() const {
    return validate_command(*this);
}

} // namespace argparse
