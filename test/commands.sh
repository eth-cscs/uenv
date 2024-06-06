echo
echo ==================== uenv image help commands

uenv image --help
echo ==================== image inspect
uenv image inspect --help
echo ==================== image find
uenv image find --help
echo ==================== image pull
uenv image pull --help
echo ==================== image ls
uenv image ls --help
echo ==================== image repo
uenv image repo --help
echo ==================== image deploy
uenv image deploy --help
echo ==================== run
uenv run --help
echo ==================== start
uenv start --help
echo ==================== stop
uenv stop --help
echo ==================== status
uenv status --help
echo ==================== modules
uenv modules --help

echo
echo ==================== uenv status
time uenv status

echo
echo ==================== uenv run prgenv-gnu -- ls /user-environment
time uenv run prgenv-gnu -- ls /user-environment

echo
echo ==================== uenv run --uarch=gh200 prgenv-gnu -- ls /user-environment
time uenv run --uarch=gh200 prgenv-gnu -- ls /user-environment

echo
echo ==================== uenv run --modules prgenv-gnu -- bash -c "module avail; ls /user-environment; module load gcc; gcc --version; which gcc;"
time uenv run --modules prgenv-gnu -- bash -c "module avail; ls /user-environment; module load gcc; gcc --version; which gcc;"

echo
echo ==================== uenv run --view=default prgenv-gnu -- bash -c "which gcc; mpic++ --version; nvcc --version;"
time uenv run --view=default prgenv-gnu -- bash -c "which gcc; mpic++ --version; nvcc --version;"

echo
echo ==================== uenv image ls
time uenv image ls

echo
echo ==================== uenv image ls prgenv-gnu
time uenv image ls prgenv-gnu

echo
echo ==================== uenv image ls prgenv-gnu/24.2:v2
time uenv image ls prgenv-gnu/24.2:v2

echo
echo ==================== uenv image find
time uenv image find
echo
echo ==================== uenv image find prgenv-gnu
time uenv image find prgenv-gnu
echo
echo ==================== uenv image find prgenv-gnu/24.2:v2
time uenv image find prgenv-gnu/24.2:v2
echo
echo ==================== uenv image find prgenv-gnu:v2
time uenv image find prgenv-gnu:v2
echo
echo ==================== uenv image find --build prgenv-gnu
time uenv image find --build prgenv-gnu
echo
echo ==================== uenv image find --build
time uenv image find --build

echo
echo ==================== uenv image inspect prgenv-gnu/24.2:v2
time uenv image inspect prgenv-gnu/24.2:v2
echo
echo ==================== uenv image inspect prgenv-gnu/24.2:v2 --format "{name}/{version}:{tag}"
time uenv image inspect prgenv-gnu/24.2:v2 --format "{name}/{version}:{tag}"
echo
echo ==================== uenv image inspect prgenv-gnu/cow:dog
time uenv image inspect prgenv-gnu/cow:dog
echo
echo ==================== uenv image find prgenv-gnu/cow:dog
time uenv image find prgenv-gnu/cow:dog


# EXPECT ERRORS
#
echo
echo ==================== uenv run --uarch=zen2 prgenv-gnu -- ls /user-environment
time uenv run --uarch=zen2 prgenv-gnu -- ls /user-environment

echo
echo ==================== uenv stop
time uenv stop

