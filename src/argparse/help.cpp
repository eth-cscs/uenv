#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <argparse/argparse.h>

namespace argparse {

namespace {

// the width of the left column in the lists of arguments: an entry wider than
// this has its description on the following line.
constexpr std::size_t column = 28;

// a list of (entry, description) pairs, formatted in two columns
std::string
two_columns(const std::vector<std::pair<std::string, std::string>>& rows) {
    std::string out;
    for (auto& [entry, description] : rows) {
        if (entry.size() + 2 < column) {
            out += fmt::format("  {:<{}}{}\n", entry, column - 2, description);
        } else {
            out += fmt::format("  {}\n  {:<{}}{}\n", entry, "", column - 2,
                               description);
        }
    }
    return out;
}

std::string positional_entry(const positional& p) {
    return p.is_rest() ? p.name() + "..." : p.name();
}

std::string option_entry(const option& o) {
    std::string entry;
    if (auto s = o.short_name()) {
        entry = fmt::format("-{}", *s);
        if (!o.long_name().empty()) {
            entry += ", ";
        }
    } else {
        // align long names with those that follow a short name
        entry = "    ";
    }
    if (!o.long_name().empty()) {
        entry += "--" + o.long_name();
    }
    if (auto n = o.negation_name()) {
        entry += fmt::format(", --{}", *n);
    }
    if (o.takes_value()) {
        entry += " " + o.metavar();
    }
    return entry;
}

} // namespace

std::string render_help(const command& cmd) {
    std::string out;

    if (!cmd.description().empty()) {
        out += cmd.description() + "\n";
    }

    // usage line
    auto usage = fmt::format("Usage: {} [OPTIONS]", fmt::join(cmd.path(), " "));
    for (auto p : cmd.positionals()) {
        auto name = positional_entry(*p);
        usage += p->is_required() ? " " + name : " [" + name + "]";
    }
    if (!cmd.subcommands().empty()) {
        usage += " SUBCOMMAND";
    }
    out += usage + "\n";

    if (auto ps = cmd.positionals(); !ps.empty()) {
        std::vector<std::pair<std::string, std::string>> rows;
        for (auto p : ps) {
            rows.emplace_back(positional_entry(*p),
                              p->is_required() ? p->help() + " (required)"
                                               : p->help());
        }
        out += "\nPositionals:\n" + two_columns(rows);
    }

    std::vector<std::pair<std::string, std::string>> rows;
    for (auto o : cmd.options()) {
        rows.emplace_back(option_entry(*o), o->is_required()
                                                ? o->help() + " (required)"
                                                : o->help());
    }
    out += "\nOptions:\n" + two_columns(rows);

    if (auto subs = cmd.subcommands(); !subs.empty()) {
        std::vector<std::pair<std::string, std::string>> rows;
        for (auto c : subs) {
            rows.emplace_back(c->name(), c->description());
        }
        out += "\nSubcommands:\n" + two_columns(rows);
    }

    if (auto footer = cmd.footer_text(); !footer.empty()) {
        out += "\n" + footer + "\n";
    }

    return out;
}

} // namespace argparse
