# bash completion for uenv
#
# uenv completes its own command lines: this script passes the words on the
# command line to `uenv __complete`, and turns the candidates it prints into
# COMPREPLY. See src/cli/complete.h for the protocol.

# Set _uenv_words, _uenv_cword and _uenv_first from COMP_WORDS: bash splits a
# word like --repo=/x or app/1.0:v1 at the characters in COMP_WORDBREAKS, so
# the parts are joined again where the command line has no space between
# them. The word at the cursor is cut at the cursor. _uenv_first holds the
# index in COMP_WORDS of the first part of each word.
_uenv_join_words() {
    _uenv_words=()
    _uenv_first=()
    _uenv_cword=0
    local line=$COMP_LINE pos=0 start=0 cword_start=0 i w space
    for ((i = 0; i < ${#COMP_WORDS[@]}; i++)); do
        w=${COMP_WORDS[i]}
        space=0
        while [[ ${line:pos:1} == [[:space:]] ]]; do
            ((pos++))
            space=1
        done
        if [[ ${line:pos:${#w}} != "$w" ]]; then
            # the words do not match the line: use them as they are
            _uenv_words=("${COMP_WORDS[@]}")
            _uenv_first=("${!COMP_WORDS[@]}")
            _uenv_cword=$COMP_CWORD
            return
        fi
        if ((i == 0 || space)); then
            _uenv_words+=("$w")
            _uenv_first+=("$i")
            start=$pos
        else
            _uenv_words[-1]+=$w
        fi
        if ((i == COMP_CWORD)); then
            _uenv_cword=$((${#_uenv_words[@]} - 1))
            cword_start=$start
        fi
        ((pos += ${#w}))
    done
    _uenv_words[_uenv_cword]=${line:cword_start:COMP_POINT-cword_start}
}

_uenv_complete() {
    local _uenv_words _uenv_first _uenv_cword
    _uenv_join_words

    local prog=${_uenv_words[0]}
    [[ $prog == "~/"* ]] && prog=$HOME/${prog#"~/"}
    local out
    out=$(command "$prog" __complete --cword="$_uenv_cword" -- "${_uenv_words[@]}" 2>/dev/null) || return
    local -a lines
    mapfile -t lines <<<"$out"
    local directive=${lines[-1]}
    [[ $directive == :* ]] || return
    unset 'lines[-1]'

    local -a directives
    IFS=, read -ra directives <<<"${directive#:}"
    local d filenames=0 nospace=0
    for d in "${directives[@]}"; do
        case $d in
        nospace)
            nospace=1
            ;;
        filenames)
            filenames=1
            ;;
        command-offset=*)
            _uenv_command_offset "${d#command-offset=}"
            return
            ;;
        esac
    done
    # readline marks the directories among file names itself, with a '/' and
    # no space after it
    if ((filenames)); then
        compopt -o filenames
    elif ((nospace)); then
        compopt -o nospace
    fi

    # readline replaces the text after the last word break character in the
    # word (e.g. = or :), or from it if it is @ or $, which bash keeps in the
    # word: remove what comes before that from every candidate
    local cur=${_uenv_words[_uenv_cword]} i c strip=
    for ((i = 0; i < ${#cur}; i++)); do
        c=${cur:i:1}
        if [[ $c != [[:space:]\"\'] && $COMP_WORDBREAKS == *"$c"* ]]; then
            if [[ $c == [@$] ]]; then
                strip=${cur:0:i}
            else
                strip=${cur:0:i+1}
            fi
        fi
    done

    COMPREPLY=()
    local l value
    for l in "${lines[@]}"; do
        value=${l%%$'\t'*}
        ((filenames)) && [[ $value == ?*/ ]] && value=${value%/}
        [[ $value == "$strip"* ]] && COMPREPLY+=("${value:${#strip}}")
    done
}

# complete the words from word $1 on as a command line of their own
_uenv_command_offset() {
    local offset=${_uenv_first[$1]:-$COMP_CWORD}
    if declare -F _comp_command_offset >/dev/null; then
        _comp_command_offset "$offset"
    elif declare -F _command_offset >/dev/null; then
        _command_offset "$offset"
    elif ((COMP_CWORD == offset)); then
        mapfile -t COMPREPLY < <(compgen -c -- "${COMP_WORDS[COMP_CWORD]}")
    else
        compopt -o default
        COMPREPLY=()
    fi
}

complete -o nosort -F _uenv_complete uenv 2>/dev/null || complete -F _uenv_complete uenv
