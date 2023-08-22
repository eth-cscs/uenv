#!/bin/bash

function setup() {
    bats_load_library bats-support
    bats_load_library bats-assert
    SQFSDIR=$(mktemp -d)
    export SQFSDIR
    dd=$(mktemp -d)
    (
        umask 022
        cd "$dd" || exit
        mkdir spack-install
        echo "This is file A" >> spack-install/fileA.txt
    )

    mksquashfs "$dd"  ${SQFSDIR}/binaries.sqfs  -quiet -noappend && rm -r "$dd"

    dd=$(mktemp -d)
    (
        umask 022
        cd "$dd" || exit
        mkdir profilers
        echo "profiler stack" >> profilers/fileB.txt
    )
    mksquashfs "$dd"  ${SQFSDIR}/profilers.sqfs -quiet  -noappend && rm -r "$dd"

    dd=$(mktemp -d)
    (
        umask 022
        cd "$dd" || exit
        mkdir tools
        echo "tools stack" >> tools/fileB.txt
    )
    mksquashfs "$dd"  ${SQFSDIR}/tools.sqfs -quiet -noappend && rm -r "$dd"
}

function teardown() {
    rm -r ${SQFSDIR}
}


@test "mount_single_image" {
    run squashfs-mount ${SQFSDIR}/binaries.sqfs -- cat /user-environment/spack-install/fileA.txt
}

#@test "mount_images" {
#    run squashfs-mount ${SQFSDIR}/binaries.sqfs:/user-environment ${SQFSDIR}/profilers.sqfs:/user-profilers ${SQFSDIR}/tools.sqfs:/user-tools -- cat /user-environment/spack-install/fileA.txt
#}


#@test "check_environment_variable" {
#    run bash -c 'squashfs-mount ${SQFSDIR}/binaries.sqfs:/user-environment ${SQFSDIR}/profilers.sqfs:/user-profilers ${SQFSDIR}/tools.sqfs:/user-tools -- (env | grep UENV_MOUNT_LIST)'
}

#@test "invalid_argument" {
#    run squashfs-mount invalid_argument -- true
#    assert_failure 1
#}

#@test "noop" {
#    # check that no namespace is unshared, when nothing is mounted
#    original_mnt=$(readlink /proc/$$/ns/mnt)
#    run bash -c 'squashfs-mount -- readlink /proc/$$/ns/mnt'
#    assert_output --partial ${original_mnt}
#}
