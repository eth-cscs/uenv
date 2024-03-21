import os
import pathlib
import shutil

prefix = pathlib.Path(__file__).parent.resolve()
prefix = prefix.parent

scratch = prefix / "scratch"

def make_scratch_path(path):
    scratch.mkdir(parents=True, exist_ok=True)

    full_path = scratch / path
    if full_path.is_dir():
        shutil.rmtree(full_path)

    full_path.mkdir()
    return full_path
