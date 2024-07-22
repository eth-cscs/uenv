import json
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


def pull_uenv(source_address, image_path, target, pull_meta, pull_sqfs):
    # download the image using oras
    try:
        terms = source_address.rsplit(":", 1)
        source_address = terms[0]
        tag = terms[1]

        if pull_meta:
            if os.path.exists(image_path+"/meta"):
                shutil.rmtree(image_path+"/meta")
                time.sleep(0.05)

            # step 1: download the meta data if there is any
            #oras discover -o json --artifact-type 'uenv/meta' jfrog.svc.cscs.ch/uenv/deploy/eiger/zen2/cp2k/2023:v1
            terminal.info(f"discovering meta data")
            proc = run_command_internal(["discover", "-o", "json", "--artifact-type", "uenv/meta", f"{source_address}:{tag}"])
            stdout, stderr = proc.communicate()
            if proc.returncode == 0:
                terminal.info(f"successfully downloaded meta data info: {stdout}")
            else:
                msg = error_message_from_stderr(stderr)
                terminal.error(f"failed to find meta data: {stderr}\n{msg}")

            manifests = json.loads(stdout)["manifests"]
            if len(manifests)==0:
                terminal.error(f"no meta data is available")

            digest = manifests[0]["digest"]
            terminal.info(f"meta data digest: {digest}")

            proc = run_command_internal(["pull", "-o", image_path, f"{source_address}@{digest}"])
            stdout, stderr = proc.communicate()

        if pull_sqfs:
            sqfs_path = image_path + "/store.squashfs"
            if os.path.exists(sqfs_path):
                os.remove(sqfs_path)
                time.sleep(0.05)

            # step 2: download the image itself
            # run the oras command in a separate process so that this process can
            # draw a progress bar.
            proc = run_command_internal(["pull", "-o", image_path, f"{source_address}:{tag}"])

            total_mb = target.size/(1024*1024)
            while proc.poll() is None:
                time.sleep(0.25)
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
