#include <algorithm>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <argparse/argparse.h>

namespace argparse {

// grants the implementation of apply() access to the callbacks stored in the
// tree
struct access {
    static void apply(const option& o, std::span<const occurrence> occ) {
        if (o.apply_) {
            o.apply_(occ);
        }
    }
    static void apply(const positional& p,
                      std::span<const std::string_view> values) {
        if (p.apply_) {
            p.apply_(values);
        }
    }
    // a counting flag is set even when it was not given (to zero)
    static bool always_applied(const option& o) {
        return o.type_ == option::type::counter;
    }
    static void selected(const command& c) {
        if (c.on_selected_) {
            c.on_selected_();
        }
    }
};

namespace {

// the words on the command line are arbitrary user input: quote them in
// messages so that empty and whitespace-only words are visible.
std::string quoted(std::string_view w) {
    return fmt::format("'{}'", w);
}

class parser {
  public:
    parser(const command& root, std::span<const std::string_view> words)
        : words_(words), cmd_(&root) {
        r_.path.push_back(&root);
    }

    parse_result run() {
        for (std::size_t i = 0; i < words_.size(); ++i) {
            word(i, words_[i]);
        }
        finish();
        return std::move(r_);
    }

  private:
    std::span<const std::string_view> words_;
    const command* cmd_;
    parse_result r_;
    // the number of times each option has been given
    std::map<const option*, unsigned> count_;

    void record(item it) {
        r_.items.push_back(std::move(it));
    }

    void fail(error_kind kind, std::optional<std::size_t> word,
              std::string message) {
        r_.errors.push_back({kind, cmd_, word, std::move(message)});
    }

    void unrecognised(std::size_t i) {
        record({.word = i, .kind = item_kind::unrecognised, .cmd = cmd_});
    }

    void word(std::size_t i, std::string_view w) {
        if (r_.pending) {
            auto opt = r_.pending;
            r_.pending = nullptr;
            record({.word = i,
                    .kind = item_kind::option_value,
                    .cmd = cmd_,
                    .opt = opt,
                    .value = w});
            check_value(i, *opt, w);
            return;
        }
        if (r_.end_of_options || r_.in_rest) {
            positional_word(i, w);
            return;
        }
        if (w == "--") {
            r_.end_of_options = true;
            record({.word = i, .kind = item_kind::end_of_options, .cmd = cmd_});
            return;
        }
        if (w.starts_with("--")) {
            long_option(i, w);
            return;
        }
        if (w.size() > 1 && w.front() == '-') {
            short_options(i, w);
            return;
        }
        if (!cmd_->subcommands().empty()) {
            subcommand(i, w);
            return;
        }
        positional_word(i, w);
    }

    // count an occurrence of an option, and check that an option that takes
    // a value is not repeated.
    void occurred(std::size_t i, const option& opt) {
        auto n = ++count_[&opt];
        if (opt.takes_value() && n == 2) {
            fail(error_kind::repeated_option, i,
                 fmt::format("option {} can only be given once",
                             opt.display_name()));
        }
    }

    void flag(std::size_t i, const option& opt, bool negated) {
        occurred(i, opt);
        record({.word = i,
                .kind = item_kind::flag,
                .cmd = cmd_,
                .opt = &opt,
                .negated = negated});
        if (opt.is_help()) {
            r_.help = cmd_;
        }
    }

    void check_value(std::size_t i, const option& opt, std::string_view v) {
        const auto& choices = opt.choices();
        if (!choices.empty() &&
            std::find(choices.begin(), choices.end(), v) == choices.end()) {
            fail(error_kind::invalid_choice, i,
                 fmt::format("invalid value {} for option {}, expected one "
                             "of {}",
                             quoted(v), opt.display_name(),
                             fmt::join(choices, ", ")));
        }
    }

    // --name, --name=value
    void long_option(std::size_t i, std::string_view w) {
        auto body = w.substr(2);
        auto eq = body.find('=');
        auto name = body.substr(0, eq);
        std::optional<std::string_view> value;
        if (eq != std::string_view::npos) {
            value = body.substr(eq + 1);
        }

        auto opt = cmd_->find_long(name);
        if (!opt) {
            // report the option without its value
            fail(error_kind::unknown_option, i,
                 fmt::format("unknown option {}",
                             quoted(w.substr(0, 2 + name.size()))));
            unrecognised(i);
            return;
        }
        if (!opt->takes_value()) {
            if (value) {
                fail(error_kind::flag_with_value, i,
                     fmt::format("{} is a flag, it does not take a value",
                                 opt->display_name()));
                unrecognised(i);
                return;
            }
            flag(i, *opt, name != opt->long_name());
            return;
        }
        option_name(i, *opt, value);
    }

    // an option that takes a value, whose value is either given in the same
    // word (--name=value, -nvalue) or is the next word
    void option_name(std::size_t i, const option& opt,
                     std::optional<std::string_view> value) {
        occurred(i, opt);
        record({.word = i,
                .kind = item_kind::option,
                .cmd = cmd_,
                .opt = &opt,
                .value = value});
        if (value) {
            check_value(i, opt, *value);
        } else {
            r_.pending = &opt;
        }
    }

    // -abc: a cluster of short options
    void short_options(std::size_t i, std::string_view w) {
        for (std::size_t k = 1; k < w.size(); ++k) {
            auto opt = cmd_->find_short(w[k]);
            if (!opt) {
                fail(error_kind::unknown_option, i,
                     fmt::format("unknown option {}",
                                 quoted(std::string{'-', w[k]})));
                unrecognised(i);
                return;
            }
            if (opt->takes_value()) {
                auto rest = w.substr(k + 1);
                option_name(i, *opt,
                            rest.empty() ? std::nullopt : std::optional{rest});
                return;
            }
            flag(i, *opt, false);
        }
    }

    void subcommand(std::size_t i, std::string_view w) {
        auto sub = cmd_->find_subcommand(w);
        if (!sub) {
            std::vector<std::string_view> names;
            for (auto c : cmd_->subcommands()) {
                names.push_back(c->name());
            }
            fail(error_kind::unknown_command, i,
                 fmt::format("unknown command {}, expected one of {}",
                             quoted(w), fmt::join(names, ", ")));
            unrecognised(i);
            return;
        }
        cmd_ = sub;
        r_.path.push_back(sub);
        r_.next_positional = 0;
        record({.word = i, .kind = item_kind::subcommand, .cmd = sub});
    }

    void positional_word(std::size_t i, std::string_view w) {
        auto ps = cmd_->positionals();
        if (r_.next_positional >= ps.size()) {
            fail(error_kind::unexpected_argument, i,
                 fmt::format("unexpected argument {}", quoted(w)));
            unrecognised(i);
            return;
        }
        auto p = ps[r_.next_positional];
        record({.word = i,
                .kind = item_kind::positional,
                .cmd = cmd_,
                .pos = p,
                .value = w});
        if (p->is_rest()) {
            r_.in_rest = true;
        } else {
            ++r_.next_positional;
        }
    }

    // checks that can only be made once every word has been seen
    void finish() {
        std::optional<std::size_t> last;
        if (!words_.empty()) {
            last = words_.size() - 1;
        }
        if (r_.pending) {
            fail(error_kind::missing_value, last,
                 fmt::format("option {} requires a value",
                             r_.pending->display_name()));
        }
        for (auto c : r_.path) {
            for (auto o : c->options()) {
                if (o->is_required() && !count_.contains(o)) {
                    r_.errors.push_back(
                        {error_kind::missing_option, c, std::nullopt,
                         fmt::format("the option {} is required",
                                     o->display_name())});
                }
            }
        }
        auto ps = cmd_->positionals();
        // a started rest positional is filled
        auto filled = r_.next_positional + (r_.in_rest ? 1 : 0);
        for (auto k = filled; k < ps.size(); ++k) {
            if (ps[k]->is_required()) {
                fail(error_kind::missing_positional, std::nullopt,
                     fmt::format("the argument {} is required", ps[k]->name()));
            }
        }
    }
};

} // namespace

parse_result parse(const command& root,
                   std::span<const std::string_view> words) {
    return parser(root, words).run();
}

parse_result parse(const command& root, std::span<const std::string> words) {
    // the values in the result view the strings in `words`, not `views`
    std::vector<std::string_view> views(words.begin(), words.end());
    return parse(root, std::span<const std::string_view>(views));
}

parse_result parse(const command& root, int argc, const char* const* argv) {
    std::vector<std::string_view> views;
    for (int i = 1; i < argc; ++i) {
        views.emplace_back(argv[i]);
    }
    return parse(root, std::span<const std::string_view>(views));
}

util::expected<applied, error> apply(const parse_result& r) {
    if (r.help) {
        return applied{.help = r.help};
    }
    if (!r.errors.empty()) {
        return util::unexpected(r.errors.front());
    }

    // gather the occurrences of each option and the values of each
    // positional, in the order they were given
    std::map<const option*, std::vector<occurrence>> options;
    std::map<const positional*, std::vector<std::string_view>> positionals;
    std::vector<const option*> option_order;
    std::vector<const positional*> positional_order;
    for (auto& it : r.items) {
        switch (it.kind) {
        case item_kind::flag:
            if (!options.contains(it.opt)) {
                option_order.push_back(it.opt);
            }
            options[it.opt].push_back({.negated = it.negated});
            break;
        case item_kind::option:
        case item_kind::option_value:
            // an option name whose value is in the next word is counted
            // when the value is seen
            if (!it.value) {
                break;
            }
            if (!options.contains(it.opt)) {
                option_order.push_back(it.opt);
            }
            options[it.opt].push_back({.value = *it.value});
            break;
        case item_kind::positional:
            if (!positionals.contains(it.pos)) {
                positional_order.push_back(it.pos);
            }
            positionals[it.pos].push_back(*it.value);
            break;
        default:
            break;
        }
    }

    for (auto c : r.path) {
        for (auto o : c->options()) {
            if (access::always_applied(*o) && !options.contains(o)) {
                access::apply(*o, {});
            }
        }
    }
    for (auto o : option_order) {
        access::apply(*o, options[o]);
    }
    for (auto p : positional_order) {
        access::apply(*p, positionals[p]);
    }
    for (auto c : r.path) {
        access::selected(*c);
    }
    return applied{};
}

} // namespace argparse
