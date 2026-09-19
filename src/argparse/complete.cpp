#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <argparse/argparse.h>
#include <argparse/complete.h>

namespace argparse {

namespace {

// Works out what the word at the cursor is, following the same rules, in the
// same order, as the parser applies to a word (see parser::word).
class completer {
  public:
    completer(const command& root, std::span<const std::string_view> words,
              std::size_t cword)
        : cword_(std::min(cword, words.size())),
          before_(parse(root, words.first(cword_))) {
        r_.cmd = &before_.selected();
        if (cword_ < words.size()) {
            r_.prefix = words[cword_];
        }
        r_.context = context(root, words);
    }

    completion_request run() {
        auto w = r_.prefix;
        const auto& cmd = *r_.cmd;
        if (before_.pending) {
            value(*before_.pending, {}, w);
        } else if (before_.end_of_options || before_.in_rest) {
            next_positional();
        } else if (w.starts_with("--")) {
            auto eq = w.find('=');
            if (eq == std::string_view::npos) {
                long_names();
            } else if (auto opt = cmd.find_long(w.substr(2, eq - 2));
                       opt && opt->takes_value()) {
                value(*opt, w.substr(0, eq + 1), w.substr(eq + 1));
            }
        } else if (w == "-") {
            long_names();
            short_names();
        } else if (w.size() > 1 && w.front() == '-') {
            short_cluster(w);
        } else if (!cmd.subcommands().empty()) {
            for (auto sub : cmd.subcommands()) {
                offer(sub->name(), sub->description());
            }
        } else if (before_.next_positional < cmd.positionals().size()) {
            next_positional();
        } else if (w.empty()) {
            // nothing else can be given: offer the options
            long_names();
        }
        return std::move(r_);
    }

  private:
    // The whole line, without what the word at the cursor contributes. A
    // value that is being typed keeps its word, so that the words after it
    // are placed as they will be, and its items are dropped. Any other word
    // (the name of an option or subcommand, perhaps with a value) is dropped
    // from the line.
    parse_result context(const command& root,
                         std::span<const std::string_view> words) const {
        if (cword_ == words.size()) {
            return parse(root, words);
        }
        if (is_value(words[cword_])) {
            auto r = parse(root, words);
            std::erase_if(r.items,
                          [this](const item& it) { return it.word == cword_; });
            std::erase_if(r.errors, [this](const error& e) {
                return e.word && *e.word == cword_;
            });
            return r;
        }
        std::vector<std::string_view> others(words.begin(), words.end());
        others.erase(others.begin() + cword_);
        auto r = parse(root, others);
        // number the words as they are in `words`
        for (auto& it : r.items) {
            it.word += it.word >= cword_;
        }
        for (auto& e : r.errors) {
            if (e.word) {
                *e.word += *e.word >= cword_;
            }
        }
        return r;
    }

    // the word at the cursor is, in its own word, the value of an option or a
    // positional argument
    bool is_value(std::string_view w) const {
        if (before_.pending || before_.end_of_options || before_.in_rest) {
            return true;
        }
        if (w.starts_with('-')) {
            return false;
        }
        return before_.selected().subcommands().empty();
    }

    std::size_t cword_;
    parse_result before_;
    completion_request r_;

    // offer `value` if it completes the word at the cursor
    void offer(std::string value, const std::string& description) {
        if (value.starts_with(r_.prefix)) {
            r_.candidates.push_back({std::move(value), description});
        }
    }

    // an option can be offered unless it has already been given anywhere on
    // the line, and giving it again is an error or has no effect
    bool available(const option& opt) const {
        if (opt.repeatable()) {
            return true;
        }
        return std::none_of(
            r_.context.items.begin(), r_.context.items.end(),
            [&](const item& it) { return it.cmd == r_.cmd && it.opt == &opt; });
    }

    void long_names() {
        for (auto opt : r_.cmd->options()) {
            if (!available(*opt)) {
                continue;
            }
            if (!opt->long_name().empty()) {
                offer("--" + opt->long_name(), opt->help());
            }
            if (auto neg = opt->negation_name()) {
                offer("--" + *neg, opt->help());
            }
        }
    }

    void short_names() {
        for (auto opt : r_.cmd->options()) {
            if (available(*opt) && opt->short_name()) {
                offer(std::string{'-', *opt->short_name()}, opt->help());
            }
        }
    }

    // -abc, or -abvVALUE where v takes a value
    void short_cluster(std::string_view w) {
        const option* last = nullptr;
        for (std::size_t k = 1; k < w.size(); ++k) {
            last = r_.cmd->find_short(w[k]);
            if (!last) {
                return;
            }
            if (last->takes_value()) {
                value(*last, w.substr(0, k + 1), w.substr(k + 1));
                return;
            }
        }
        // a cluster of flags is complete as it is
        offer(std::string(w), last->help());
    }

    void value(const option& opt, std::string_view keep,
               std::string_view prefix) {
        r_.opt = &opt;
        r_.keep = keep;
        r_.prefix = prefix;
        for (auto& c : opt.choices()) {
            if (c.starts_with(prefix)) {
                r_.candidates.push_back({std::string(keep) + c, {}});
            }
        }
    }

    void next_positional() {
        auto ps = r_.cmd->positionals();
        if (before_.in_rest) {
            r_.pos = ps.back();
            // the first word given to the rest positional
            for (auto& it : before_.items) {
                if (it.pos == r_.pos) {
                    r_.rest_start = it.word;
                    break;
                }
            }
            return;
        }
        if (before_.next_positional < ps.size()) {
            r_.pos = ps[before_.next_positional];
            if (r_.pos->is_rest()) {
                // the word at the cursor would be its first
                r_.rest_start = cword_;
            }
        }
    }
};

} // namespace

completion_request complete(const command& root,
                            std::span<const std::string_view> words,
                            std::size_t cword) {
    return completer(root, words, cword).run();
}

} // namespace argparse
