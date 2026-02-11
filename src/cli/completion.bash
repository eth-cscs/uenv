_uenv_helper_images_ls() {
  # get local uenv images in "name/version:tag" format
  echo $(uenv image ls --json | jq -r '.records[] | "\(.name)/\(.version):\(.tag)"')
}

_uenv_helper_stack_to_funcname() {
  # convert a command stack to function name, which might not exists
  # e.g. ("uenv" "image" "ls") -> "_uenv_image_ls"
  local -a cmd_stack=("$@")
  local IFS="_"
  echo "_${cmd_stack[*]}"
}

_uenv_helper_is_cmd_path() {
  # check if the given command stack exists as a function
  # e.g. ("uenv" "image" "ls") -> checks if function "_uenv_image_ls" exists
  local -a cmd_path=("$@")
  local funcname=$(_uenv_helper_stack_to_funcname ${cmd_path[@]})
  if [[ $(type -t ${funcname}) == function ]]; then
    return 0
  else
    return 1
  fi
}

_uenv_helper_cmd_stack_function() {
  # given command parts (filtered from flags), build the command stack and call the related function.
  # command stack is built incrementally from the left up to the first non existing sub-command (excluded)

  # first arg must be the "uenv" entrypoint
  # start from there creating the call stack and consume it
  local -a cmd_stack=($1)
  shift

  # process command parts from left to right to build the command stack
  # TODO it currently consider "valid" any words, so it includes also flags values and any other non-sub-commands words
  for cmd_part; do
    # check if adding this part is still an existing "command stack"
    if _uenv_helper_is_cmd_path ${cmd_stack[@]} ${cmd_part}; then
      cmd_stack+=("$cmd_part")
    fi
  done

  # by construction it is a valid function
  local funcname=$(_uenv_helper_stack_to_funcname ${cmd_stack[@]})
  ${funcname}
}

_uenv_completions()
{
  local cur prev words cword
  _init_completion -n :

  # Create CMD_PARTS as an array of words (mainly commands and sub-commands, but not only)
  # where flags are filtered (i.e. words starting with - and -- are ignored)
  local -a CMD_PARTS
  for cmd_part in "${words[@]}"; do
    if [[ $cmd_part != -* && $cmd_part != --* ]]; then
      CMD_PARTS+=("${cmd_part}")
    fi
  done

  # Each (sub-)command has its own function for loading related metadata (e.g. sub-commands, options, ...)
  local UENV_SUBCMDS
  local UENV_NONPOSITIONALS
  local UENV_ACCEPT_LABEL
  _uenv_helper_cmd_stack_function ${CMD_PARTS[@]}

  # Here metadata for the completion should be available
  case $cur in
    # flags are shown just if user started writing a dash '-'. in case, suggest flags only.
    -*)
      COMPREPLY=($(compgen -W "${UENV_NONPOSITIONALS}" -- "${cur}"))
      return 0
      ;;
    *)
      # add sub-commands to list of suggestions (compatible with user hint)
      local SUBCOMMAND_OPTS=$(compgen -W "${UENV_SUBCMDS}" -- "${cur}")
      COMPREPLY=(${SUBCOMMAND_OPTS})

      # TODO skip if uenv label has been specified
      # if the command accepts a uenv label add also available uenvs labels
      if [ "${UENV_ACCEPT_LABEL}" = "true" ]; then

        local -a UENVS_LOCAL=$(_uenv_helper_images_ls)
        COMPREPLY+=($(compgen -W "${UENVS_LOCAL[*]}" -- "${cur}"))

        # note: uenv label might contain a colon, so we need to trim COMPREPLY (only if list is not empty)
        if [ ${#UENVS_LOCAL[*]} -gt 0 ]; then
          __ltrim_colon_completions "$cur"
        fi

        # if hint looks like a path, or there is no other option, show file completions
        if [[ $cur == ./* || $cur == /* || ${#COMPREPLY[@]} -eq 0 ]]; then
          # show dirs + *.squashfs files
          local -a FILE_OPTS=($(compgen -o plusdirs -f -X!*.squashfs -- "${cur}"))

          # disable automatic space after hints, so we can manage it as we like:
          # - no space after dirs: we want to keep completing this part until a file is selected
          # - add space after file: a file is a valid uenv label, so accept and pass to next part
          compopt -o nospace
          for item in "${FILE_OPTS[@]}"; do
            if [[ -d "${item}" ]]; then
              COMPREPLY+=("${item}/")
            else
              COMPREPLY+=("${item} ")
            fi
          done
        fi
      fi
      ;;
  esac
}

complete -F _uenv_completions uenv
