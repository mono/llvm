#!/usr/bin/env bash

set -e

# Requires packages:
# devscripts cmake ninja git

TOP_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )/../../" && pwd )"

echo "MAKING DEB"

mkdir -p $TOP_DIR/build/mono-llvm-6.0
pushd $TOP_DIR/build/mono-llvm-6.0

cp -r $TOP_DIR/scripts/ci/debian debian

dpkg-buildpackage -d -us -uc

popd # $TOP_DIR/build/mono-llvm-6.0

echo "DONE"


