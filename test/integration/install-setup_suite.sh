#/bin/bash

src=$1
dst=$2
source_path=$3
build_path=$4

cp $src $dst
sed -i -e"s|@build_path@|${build_path}|g" -e"s|@source_path@|${source_path}|g" $dst
