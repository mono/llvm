//===- IntrinsicsMono.td - Mono specific LLVM intrinsics ---------*- tablegen -*-===//

//
// Same as the load instruction, but can throw an exception, has to be called using
// invoke
// mono.load (addr, align, is_volatile)
// mono.store (value, addr, align, is_volatile)
//
def int_mono_load : Intrinsic<[llvm_anyint_ty],
                              [LLVMAnyPointerType<LLVMMatchType<0>>,
							   llvm_i32_ty, llvm_i1_ty],
                              [IntrReadArgMem, Throws, NoCapture<0>]>;

def int_mono_store : Intrinsic<[],
                              [llvm_anyint_ty,
                               LLVMAnyPointerType<LLVMMatchType<0>>,
							   llvm_i32_ty, llvm_i1_ty],
                              [IntrReadWriteArgMem, Throws]>;

//
// Same as sdiv, but can throw an exception
//
def int_mono_sdiv : Intrinsic<[llvm_anyint_ty],
                             [llvm_anyint_ty, llvm_anyint_ty],
                             [Throws]>;
