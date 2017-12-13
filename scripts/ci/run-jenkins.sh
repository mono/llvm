#!/bin/bash -e

echo "ENVIRONMENT:"
env

#COMMON_ACVARS="ac_cv_func_fstatat=no ac_cv_func_readlinkat=no ac_cv_func_futimens=no ac_cv_func_utimensat=no"

LLVM_BASE_CONFIGURE_FLAGS="--enable-libcpp --enable-optimized --enable-assertions=no --disable-jit --disable-docs --disable-doxygen"
#LLVM_BASE_CONFIGURE_ENVIRONMENT="$COMMON_ACVARS"

mkdir -p build
cd build
../configure --prefix=$PWD/usr --enable-targets="arm arm64" $LLVM_BASE_CONFIGURE_FLAGS CXXFLAGS="-Qunused-arguments"
make -j4
make install
mkdir tmp-bin
cp usr/bin/{llc,opt,llvm-dis,llvm-config} tmp-bin/
rm usr/bin/*
cp tmp-bin/* usr/bin/
rm -f usr/lib/libLTO.* usr/lib/*.dylib
tar cvzf llvm-osx64-$GIT_COMMIT.tar.gz usr
