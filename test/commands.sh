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

