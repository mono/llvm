#!/bin/bash -e

echo "ENVIRONMENT:"
env

llvm_base_CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD=X86;ARM;AArch64 -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=WebAssembly -DLLVM_BUILD_TESTS=Off -DLLVM_INCLUDE_TESTS=Off -DLLVM_TOOLS_TO_BUILD=opt;llc;llvm-config;llvm-dis -G Ninja"

llvm64_CMAKE_FLAGS="$llvm_base_CMAKE_FLAGS"
llvm32_CMAKE_FLAGS="$llvm_base_CMAKE_FLAGS -DLLVM_BUILD_32_BITS=On"

rm -rf build
mkdir -p build
cd build
cmake $llvm64_CMAKE_FLAGS -DCMAKE_INSTALL_PREFIX=$PWD/../usr64 ../
ninja
ninja install
cd ..
rm -rf build32
mkdir -p build32
cd build32
cmake $llvm32_CMAKE_FLAGS -DCMAKE_INSTALL_PREFIX=$PWD/../usr32 ../
ninja
ninja install
cd ..

rm -rf tmp-bin
mkdir tmp-bin
cp usr64/bin/{llc,opt,llvm-dis,llvm-config} tmp-bin/
rm usr64/bin/*
cp tmp-bin/* usr64/bin/

rm -rf tmp-bin2
mkdir tmp-bin2
cp usr32/bin/{llc,opt,llvm-dis,llvm-config} tmp-bin2
rm usr32/bin/*
cp tmp-bin2/* usr32/bin/
# Don't need 32 bit binaries
rm -f usr64/lib/libLTO.* usr64/lib/*.dylib usr32/lib/libLTO.* usr32/lib/*.dylib
tar cvzf llvm-osx64-$GIT_COMMIT.tar.gz usr64 usr32
