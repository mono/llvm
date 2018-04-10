#!/bin/bash -e

echo "ENVIRONMENT:"
env

#COMMON_ACVARS="ac_cv_func_fstatat=no ac_cv_func_readlinkat=no ac_cv_func_futimens=no ac_cv_func_utimensat=no"

LLVM_BASE_CONFIGURE_FLAGS="--enable-libcpp --enable-optimized --enable-assertions=no --disable-jit --disable-docs --disable-doxygen"
#LLVM_BASE_CONFIGURE_ENVIRONMENT="$COMMON_ACVARS"

mkdir -p build
cd build
../configure --prefix=$PWD/../usr64 --enable-targets="arm arm64 x86 x86_64" $LLVM_BASE_CONFIGURE_FLAGS CXXFLAGS="-Qunused-arguments"
make -j4
make install
cd ..
mkdir -p build32
cd build32
../configure --prefix=$PWD/../usr32 --build=i386-apple-darwin11.2.0 --enable-targets="arm arm64" $LLVM_BASE_CONFIGURE_FLAGS CXXFLAGS="-Qunused-arguments"
make -j4
make install
cd ..
mkdir tmp-bin
cp usr64/bin/{llc,opt,llvm-dis,llvm-config} tmp-bin/
rm usr64/bin/*
cp tmp-bin/* usr64/bin/
mkdir tmp-bin2
cp usr32/bin/llvm-config tmp-bin2
rm usr32/bin/*
cp tmp-bin2/* usr32/bin/
# Don't need 32 bit binaries
rm -f usr64/lib/libLTO.* usr64/lib/*.dylib usr32/lib/libLTO.* usr32/lib/*.dylib
tar cvzf llvm-osx64-$GIT_COMMIT.tar.gz usr64 usr32
