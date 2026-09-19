#pragma once

// Tab completion of a command line, using the same parser as a real
// invocation.
//
// `complete()` works out what the word at the cursor is expected to be (a
// subcommand name, an option name, the value of an option, or a positional
// argument) and offers the candidates that the tree itself knows about:
// subcommand names, option names and the values of choice options. The values
// of every other argument are left to the application, which reads the
// argument's `completion` and uses the parse of the whole line for context.

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <argparse/argparse.h>

namespace argparse {

struct candidate {
    // a replacement for the whole word at the cursor
    std::string value;
    std::string description;
};

struct completion_request {
    // the command among whose words the cursor is
    const command* cmd = nullptr;
    // the value being completed belongs to this option, or to this positional
    // argument; both are null when a name is being completed, or when nothing
    // can be given at the cursor.
    const option* opt = nullptr;
    const positional* pos = nullptr;
    // the part of the value typed so far
    std::string_view prefix;
    // the text in the cursor word before the value: "--view=" or "-v" when the
    // value is in the same word as the option name, otherwise empty. The
    // values of candidates offered for `opt` or `pos` start with it.
    std::string_view keep;
    // for a rest positional: the index of its first word, which is the start
    // of the command line it holds
    std::size_t rest_start = 0;
    // the names and choices that the tree knows about
    std::vector<candidate> candidates;
    // the whole command line, including the words after the cursor, without
    // the items of the word at the cursor, which is still being typed
    parse_result context;
};

// Complete word `cword` of `words` (which should not include the program
// name). The word at the cursor should be cut at the cursor; `cword` can be
// words.size(), which completes an empty word after the last one. The request
// views the words, so they must outlive it.
completion_request complete(const command& root,
                            std::span<const std::string_view> words,
                            std::size_t cword);

} // namespace argparse
