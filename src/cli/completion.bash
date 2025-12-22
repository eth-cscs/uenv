_uenv_completions()
{
    local cur prefix func_name UENV_OPTS

    local -a COMP_WORDS_NO_FLAGS
    local index=0
    while [[ "$index" -lt "$COMP_CWORD" ]]
    do
        if [[ "${COMP_WORDS[$index]}" == [a-z]* ]]
        then
            COMP_WORDS_NO_FLAGS+=("${COMP_WORDS[$index]}")
        fi
        let index++
    done
    COMP_WORDS_NO_FLAGS+=("${COMP_WORDS[$COMP_CWORD]}")
    local COMP_CWORD_NO_FLAGS=$((${#COMP_WORDS_NO_FLAGS[@]} - 1))

    cur="${COMP_WORDS_NO_FLAGS[COMP_CWORD_NO_FLAGS]}"
    prefix="_${COMP_WORDS_NO_FLAGS[*]:0:COMP_CWORD_NO_FLAGS}"
    func_name="${prefix// /_}"
    func_name="${func_name//-/_}"

    local UENV_OPTS=""
    local FILE_COMPLETION="false"
    if typeset -f $func_name >/dev/null
    then
        $func_name
    fi

    local SUBCOMMAND_OPTS=$(compgen -W "${UENV_OPTS}" -- "${cur}")
    local FILE_OPTS=""
    local FILE_OPTS_FORMATED=""
    if [ "${FILE_COMPLETION}" = "true" ]; then
        FILE_OPTS=$(compgen -f -- "${cur}")
        for item in $FILE_OPTS; do
            if [[ -d "${item}" ]]; then
                FILE_OPTS_FORMATED+="${item}/ "
            else
                FILE_OPTS_FORMATED+="${item} "
            fi
        done
    fi

    # Do not add space after suggestion if completing a directory
    local DIRS=$(compgen -d -- "${cur}")
    if [[ -n "${DIRS}" ]]; then
        compopt -o nospace
    else
        compopt +o nospace
    fi

    COMPREPLY=(${SUBCOMMAND_OPTS} ${FILE_OPTS_FORMATED})
}

complete -F _uenv_completions uenv
