import pathlib
import shutil
import subprocess

import terminal

def find_oras() -> str:
    # - the oras executable is installed in the libexec path
    # - this file is installed in the libexec/lib path
    # - the executable is named uenv-oras
    # search here for the executable
    oras_dir = pathlib.Path(__file__).parent.parent.resolve()
    oras_file = oras_dir / "uenv-oras"
    terminal.info(f"searching for oras executable: {oras_file}")
    if oras_file.is_file():
        terminal.info(f"using {oras_file}")
        return oras_file.as_posix()

    # fall back to finding the oras executable
    terminal.info(f"{oras_file} does not exist - searching for oras")
    oras_file = shutil.which("oras")
    if not oras_file:
        terminal.error(f"no oras executable found")
    terminal.info(f"using {oras_file}")
    return oras_file


def run_command(args):
    try:
        command = [find_oras()] + args

        terminal.info(f"calling oras: {' '.join(command)}")
        result = subprocess.run(
            command,
            stdout=subprocess.PIPE,  # Capture standard output
            stderr=subprocess.PIPE,  # Capture standard error
            check=True,  # Raise exception if command fails
            encoding='utf-8'  # Decode output from bytes to string
        )

        # Print standard output
        terminal.info("Output:\n{result.stdout}")

    except subprocess.CalledProcessError as e:
        # Print error message along with captured standard error
        terminal.error("An error occurred:\n", e.stderr)

