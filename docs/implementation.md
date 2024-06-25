# Implementation

## Environment Variables

### Variables set and read by UENV

* `UENV_VIEW`: the view that has been loaded (only one view can be loaded at a time

```
UENV_VIEW=/user-environment:default
```

* `UENV_CMD`: the path to the Python implementation of the tool.

For example, when installed locally by the user `bobsmith`:
```
UENV_CMD=/users/bobsmith/.local/libexec/uenv-impl
```

### Variables read by UENV

* `UENV_MOUNT_LIST`: set by `squashfs-mount` and the slurm plugin, to record which images have been mounted where.

An example of two images, mounted at `/user-environment` and `/user-tools`:
```
UENV_MOUNT_LIST=file:///scratch/bobsmith/qe.squashfs:/user-environment,file:///scratch/bobsmith/ddt.squashfs:/user-tools
```

* `SYSTEM`: a variable with the cluster name on Alps.

