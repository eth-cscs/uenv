#!/usr/bin/env bash

source="$1"
target="$2"

sudo cp $source $target
sudo chown root:root "${target}"
sudo chmod u+s "${target}"

