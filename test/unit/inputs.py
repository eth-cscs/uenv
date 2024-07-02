import os
import pathlib
import shutil

prefix = pathlib.Path(__file__).parent.resolve()
prefix = prefix.parent

input_path = prefix / "input"

def activate():
    return input_path / "activate"

def database(version):
    """
    returns the full path of a index.db created with "version" of the database schema
    """
    return input_path / "versioned_db" / f"v{version}" / "index.db"
