
# TODO:
# parse os
# parse slurm

os=X
slurm=X
ref=X

# TODO: find a way to inject the source code into the container so that ref doesn't need to be provided

container=uenv-$os

podman build -f ./dockerfiles/$os . -t $container

podman run -v $(pwd):/work:rw -w /work -it $container:latest \
    sh -c "CXX=g++-12 CC=gcc-12 ./suse-build.sh --ref=$ref --slurm-version=$slurm"

