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
  # given command parts (filtered from flags),
  # build the longest command stack and call the related function

  # first arg must be the "uenv" entrypoint
  # start from there creating the call stack and consume it
  local -a cmd_stack=($1)
  shift

  # process command parts from left to right to build the command stack (just commands/sub-commands)
  for cmd_part; do
    if _uenv_helper_is_cmd_path ${cmd_stack[@]} ${cmd_part}; then
      cmd_stack+=("$cmd_part")
    fi
  done

  local funcname=$(_uenv_helper_stack_to_funcname ${cmd_stack[@]})
  ${funcname}
}

_uenv_completions()
{
  local cur prev words cword
  _init_completion -n :

  local -a CMD_PARTS
  for cmd_part in "${words[@]}"; do
    if [[ $cmd_part != -* && $cmd_part != --* ]]; then
      CMD_PARTS+=("${cmd_part}")
    fi
  done

  local UENV_SUBCMDS
  local UENV_NONPOSITIONALS
  local UENV_ACCEPT_LABEL
  _uenv_helper_cmd_stack_function ${CMD_PARTS[@]}

  case $cur in
    -*)
      COMPREPLY=($(compgen -W "${UENV_NONPOSITIONALS}" -- "${cur}"))
      return 0
      ;;
    *)
      local SUBCOMMAND_OPTS=$(compgen -W "${UENV_SUBCMDS}" -- "${cur}")
      COMPREPLY=(${SUBCOMMAND_OPTS})

      # TODO skip if uenv label has been specified

      if [ "${UENV_ACCEPT_LABEL}" = "true" ]; then

        local -a UENVS_LOCAL=$(_uenv_helper_images_ls)
        COMPREPLY+=($(compgen -W "${UENVS_LOCAL[*]}" -- "${cur}"))

        # note: uenv label might contain a colon, so we need to trim COMPREPLY only if there are any
        if [ ${#UENVS_LOCAL[*]} -gt 0 ]; then
          __ltrim_colon_completions "$cur"
          # return 0
        fi

        # if hint looks like a path, or there is no other option, show file completions
        if [[ $cur == ./* || $cur == /* || ${#COMPREPLY[@]} -eq 0 ]]; then
          # show dirs + *.squashfs files
          local -a FILE_OPTS=($(compgen -o plusdirs -f -X!*.squashfs -- "${cur}"))

          # no space after dirs, trailing space after files
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
