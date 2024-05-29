import os
import pathlib
import shutil

prefix = pathlib.Path(__file__).parent.resolve()
prefix = prefix.parent

input_path = prefix / "input"

def activate():
    return input_path / "activate"
