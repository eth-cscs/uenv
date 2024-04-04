# arbitrary alps-specific gubbins

import os

import terminal

# the path used to store a users cached images and meta data
def uenv_repo_path(path: str=None) -> str:
    if path is not None:
        return path

    # check whether the image path has been explicitly set:
    path = os.environ.get("UENV_REPO_PATH")
    if path is not None:
        return path

    # if not, try to use the path $SCRATCH/.uenv-images/, if SCRATCH exists
    path = os.environ.get("SCRATCH")
    if path is not None:
        return path + "/.uenv-images"

    terminal.error("No repository path available: set UENV_REPO_PATH or use the --repo flag")


