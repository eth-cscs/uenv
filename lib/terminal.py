import os
import sys

colored_output = True
debug_level = 1

def is_tty():
    return sys.stdout.isatty()

# Choose whether to use colored output.
# - by default colored output is ON
# - if the flag --no-color is passed it is OFF
# - if the environment variable NO_COLOR is set it is OFF
def use_colored_output(cli_arg: bool):
    global colored_output

    # The --no-color argument overrides all environment variables if passed.
    if cli_arg:
        colored_output = False
        return

    # Check the env. var NO_COLOR and disable color if set.
    # See https://no-color.org/ for the informal spec
    #
    #    Command-line software which adds ANSI color to its output by default
    #    should check for a NO_COLOR environment variable that, when present and
    #    not an empty string (regardless of its value), prevents the addition of
    #    ANSI color.
    #
    if os.environ.get('NO_COLOR') is not None:
        color_var = os.environ.get('NO_COLOR')
        if len(color_var)>0:
            colored_output = False
            return

def colorize(string, color):
    colors = {
        "red":     "31",
        "green":   "32",
        "yellow":  "33",
        "blue":    "34",
        "magenta": "35",
        "cyan":    "36",
        "white":   "37",
        "gray":    "90",
    }
    if colored_output:
        return f"\033[1;{colors[color]}m{string}\033[0m"
    else:
        return string

def set_debug_level(level: int):
    global debug_level
    debug_level = level

def error(message, abort=True):
    print(f"{colorize('[error]', 'red')} {message}", file=sys.stderr)
    if abort:
        exit(1)

# for printing messages to the terminal
def stdout(message):
    print(f"{message}", file=sys.stdout)
def stderr(message):
    print(f"{message}", file=sys.stderr)

def warning(message):
    print(f"{colorize('[warning]', 'yellow')} {message}", file=sys.stderr)

def info(message):
    if debug_level>1:
        print(f"{colorize('[info]', 'green')} {message}", file=sys.stderr)

def exit_with_success():
    exit(0)


