#!/bin/bash -e

env
exit 0

#COMMON_ACVARS="ac_cv_func_fstatat=no ac_cv_func_readlinkat=no ac_cv_func_futimens=no ac_cv_func_utimensat=no"

LLVM_BASE_CONFIGURE_FLAGS="--enable-libcpp --enable-optimized --enable-assertions=no --disable-jit --disable-docs --disable-doxygen"
#LLVM_BASE_CONFIGURE_ENVIRONMENT="$COMMON_ACVARS"

mkdir -p build
cd build
../configure --prefix=$PWD/usr --enable-targets="arm arm64" $LLVM_BASE_CONFIGURE_FLAGS CC="ccache clang" CXX="ccache clang++"  CXXFLAGS="-Qunused-arguments"
make -j4
make install

