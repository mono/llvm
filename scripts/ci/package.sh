#!/usr/bin/env bash

set -e

# Requires packages:
# devscripts cmake ninja git

TOP_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )/../../" && pwd )"

echo "MAKING DEB"

mkdir -p $TOP_DIR/build/mono-llvm-3.9
pushd $TOP_DIR/build/mono-llvm-3.9

cp -r $TOP_DIR/scripts/ci/debian debian

dpkg-buildpackage -d -us -uc

popd # $TOP_DIR/build/mono-llvm-3.9

echo "DONE"


