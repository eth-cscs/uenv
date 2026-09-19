// vim: ts=4 sts=4 sw=4 et
#pragma once

#include <span>

#include <argparse/argparse.h>

#include "cli.h"
#include "uenv.h"

namespace uenv {

// `uenv __complete --cword=N -- WORDS...`: the completions of word N of WORDS,
// the words of a command line (WORDS[0] is the program), for the shell
// completion scripts. It is not part of the command line interface: main()
// calls it when the first argument is __complete. `args` are the arguments
// after __complete.
//
// It prints one candidate per line, as the value and a description separated
// by a tab, and then a line of directives for the shell: ':' followed by a
// comma separated list of
//
//   nospace          do not add a space after the completed word
//   filenames        the candidates are paths (quote them as file names)
//   command-offset=K the words from K on are a command line of their own:
//                    complete them as such
//
// It never writes to stderr and always returns 0: its output goes to the
// user's terminal, in the middle of a command line.
int complete_main(const argparse::program<global_args>& cli,
                  const global_settings& settings,
                  std::span<const char* const> args);

} // namespace uenv
