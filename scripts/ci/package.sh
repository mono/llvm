#!/usr/bin/env bash

set -e

# Requires packages:
# devscripts cmake ninja git

TOP_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )/../../" && pwd )"

if [ ! -d "tmp-cmake" ]; then
mkdir tmp-cmake
cd tmp-cmake
wget https://cmake.org/files/v3.5/cmake-3.5.2-Linux-x86_64.tar.gz
tar xf cmake-3.5.2-Linux-x86_64.tar.gz
cd cmake-3.5.2-Linux-x86_64/bin/
export PATH=`pwd`:$PATH
else
cd tmp-cmake/cmake-3.5.2-Linux-x86_64/bin
export PATH=`pwd`:$PATH
fi

echo "Cloning Clang"

cd $TOP_DIR/tools
 
if [ ! -d "clang" ]; then
git clone https://github.com/llvm-mirror/clang.git
cd clang
git checkout f37acf6b6adf1cc6c103981332f6db7f2002dcdc
fi

mkdir -p $TOP_DIR/build

cd $TOP_DIR/build

export PKG_DIR=$TOP_DIR/build/mono-llvm-3.9

echo "INSTALLING LLVM TO BUILD DIR"

mkdir -p $PKG_DIR

cp -r $TOP_DIR/scripts/ci/debian $PKG_DIR/debian

cd $PKG_DIR

echo "MAKING DEB"

if [ $LLVM_TARGET = "ARMV7" ]; then
dpkg-buildpackage -d -us -uc -aarmhf
fi

if [ $LLVM_TARGET = "HOST" ]; then
dpkg-buildpackage -d -us -uc
fi

echo "DONE"

cd ../


