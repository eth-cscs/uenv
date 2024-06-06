import os
import pathlib
import shutil
import subprocess
import time

import progress
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

def run_command_internal(args):
    try:
        command = [find_oras()] + args

        terminal.info(f"calling oras: {' '.join(command)}")

        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,  # Capture standard output
            stderr=subprocess.PIPE,  # Capture standard error
            encoding='utf-8'  # Decode output from bytes to string
        )
        return process

    except Exception as e:
        # Print error message along with captured standard error
        terminal.error(f"an unknown error occured with the oras client: {str(e)}")

def error_message_from_stderr(stderr: str) -> str:
        if "client does not have permission for manifest" in stderr:
            return f"{terminal.colorize('Hint', 'yellow')} check that you have permissions to pull from the target JFrog registry, or contact CSCS Service Desk with the full command and output (preferrably also with the --verbose flag)."
        return ""

def run_command(args):
    try:
        proc = run_command_internal(args)
        stdout, stderr = proc.communicate()
        if proc.returncode == 0:
            terminal.info(f"oras command successful: {stdout}")
        else:
            msg = error_message_from_stderr(stderr)
            terminal.error(f"oras command failed: {stderr}\n{msg}")
    except KeyboardInterrupt:
        proc.terminate()
        terminal.error(f"oras command cancelled by user.")
    except Exception as e:
        proc.terminate()
        terminal.error(f"oras command failed with unknown exception: {e}")


def pull_uenv(source_address, image_path, target):
    # download the image using oras
    try:
        # run the oras command in a separate process so that this process can
        # draw a progress bar.
        proc = run_command_internal(["pull", "-o", image_path, source_address])

        # remove the old path if it exists
        if os.path.exists(image_path):
            shutil.rmtree(image_path)
            time.sleep(0.2)

        sqfs_path = image_path + "/store.squashfs"
        total_mb = target.size/(1024*1024)
        while proc.poll() is None:
            time.sleep(1.0)
            if os.path.exists(sqfs_path) and terminal.is_tty():
                current_size = os.path.getsize(sqfs_path)
                current_mb = current_size / (1024*1024)
                p = current_mb/total_mb
                msg = f"{int(current_mb)}/{int(total_mb)} MB"
                progress.progress_bar(p, width=50, msg=msg)
        stdout, stderr = proc.communicate()
        if proc.returncode == 0:
            # draw a final complete progress bar
            progress.progress_bar(1.0, width=50, msg=f"{int(total_mb)}/{int(total_mb)} MB")
            terminal.stdout("")
            terminal.info(f"oras command successful: {stdout}")
        else:
            msg = error_message_from_stderr(stderr)
            terminal.error(f"image pull failed: {stderr}\n{msg}")
    except KeyboardInterrupt:
        proc.terminate()
        terminal.stdout("")
        terminal.error(f"image pull cancelled by user.")
    except Exception as e:
        proc.terminate()
        terminal.stdout("")
        terminal.error(f"image pull failed with unknown exception: {e}")
