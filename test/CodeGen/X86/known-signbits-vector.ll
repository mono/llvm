; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc < %s -mtriple=i686-unknown-unknown -mattr=+avx | FileCheck %s --check-prefix=X32
; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mattr=+avx | FileCheck %s --check-prefix=X64

define <2 x double> @signbits_sext_v2i64_sitofp_v2f64(i32 %a0, i32 %a1) nounwind {
; X32-LABEL: signbits_sext_v2i64_sitofp_v2f64:
; X32:       # BB#0:
; X32-NEXT:    vcvtdq2pd {{[0-9]+}}(%esp), %xmm0
; X32-NEXT:    retl
;
; X64-LABEL: signbits_sext_v2i64_sitofp_v2f64:
; X64:       # BB#0:
; X64-NEXT:    vmovd %edi, %xmm0
; X64-NEXT:    vpinsrd $1, %esi, %xmm0, %xmm0
; X64-NEXT:    vcvtdq2pd %xmm0, %xmm0
; X64-NEXT:    retq
  %1 = sext i32 %a0 to i64
  %2 = sext i32 %a1 to i64
  %3 = insertelement <2 x i64> undef, i64 %1, i32 0
  %4 = insertelement <2 x i64> %3, i64 %2, i32 1
  %5 = sitofp <2 x i64> %4 to <2 x double>
  ret <2 x double> %5
}

define <4 x float> @signbits_sext_v4i64_sitofp_v4f32(i8 signext %a0, i16 signext %a1, i32 %a2, i32 %a3) nounwind {
; X32-LABEL: signbits_sext_v4i64_sitofp_v4f32:
; X32:       # BB#0:
; X32-NEXT:    movsbl {{[0-9]+}}(%esp), %eax
; X32-NEXT:    movswl {{[0-9]+}}(%esp), %ecx
; X32-NEXT:    vmovd %eax, %xmm0
; X32-NEXT:    sarl $31, %eax
; X32-NEXT:    vpinsrd $1, %eax, %xmm0, %xmm0
; X32-NEXT:    vpinsrd $2, %ecx, %xmm0, %xmm0
; X32-NEXT:    sarl $31, %ecx
; X32-NEXT:    movl {{[0-9]+}}(%esp), %eax
; X32-NEXT:    movl {{[0-9]+}}(%esp), %edx
; X32-NEXT:    vmovd %eax, %xmm1
; X32-NEXT:    sarl $31, %eax
; X32-NEXT:    vpinsrd $1, %eax, %xmm1, %xmm1
; X32-NEXT:    vpinsrd $2, %edx, %xmm1, %xmm1
; X32-NEXT:    sarl $31, %edx
; X32-NEXT:    vpinsrd $3, %edx, %xmm1, %xmm1
; X32-NEXT:    vpinsrd $3, %ecx, %xmm0, %xmm0
; X32-NEXT:    vshufps {{.*#+}} xmm0 = xmm0[0,2],xmm1[0,2]
; X32-NEXT:    vcvtdq2ps %xmm0, %xmm0
; X32-NEXT:    retl
;
; X64-LABEL: signbits_sext_v4i64_sitofp_v4f32:
; X64:       # BB#0:
; X64-NEXT:    movslq %edi, %rax
; X64-NEXT:    movslq %esi, %rsi
; X64-NEXT:    movslq %edx, %rdx
; X64-NEXT:    movslq %ecx, %rcx
; X64-NEXT:    vmovq %rcx, %xmm0
; X64-NEXT:    vmovq %rdx, %xmm1
; X64-NEXT:    vpunpcklqdq {{.*#+}} xmm0 = xmm1[0],xmm0[0]
; X64-NEXT:    vmovq %rsi, %xmm1
; X64-NEXT:    vmovq %rax, %xmm2
; X64-NEXT:    vpunpcklqdq {{.*#+}} xmm1 = xmm2[0],xmm1[0]
; X64-NEXT:    vshufps {{.*#+}} xmm0 = xmm1[0,2],xmm0[0,2]
; X64-NEXT:    vcvtdq2ps %xmm0, %xmm0
; X64-NEXT:    retq
  %1 = sext i8 %a0 to i64
  %2 = sext i16 %a1 to i64
  %3 = sext i32 %a2 to i64
  %4 = sext i32 %a3 to i64
  %5 = insertelement <4 x i64> undef, i64 %1, i32 0
  %6 = insertelement <4 x i64> %5, i64 %2, i32 1
  %7 = insertelement <4 x i64> %6, i64 %3, i32 2
  %8 = insertelement <4 x i64> %7, i64 %4, i32 3
  %9 = sitofp <4 x i64> %8 to <4 x float>
  ret <4 x float> %9
}

define float @signbits_ashr_extract_sitofp_0(<2 x i64> %a0) nounwind {
; X32-LABEL: signbits_ashr_extract_sitofp_0:
; X32:       # BB#0:
; X32-NEXT:    pushl %eax
; X32-NEXT:    vextractps $1, %xmm0, %eax
; X32-NEXT:    vcvtsi2ssl %eax, %xmm1, %xmm0
; X32-NEXT:    vmovss %xmm0, (%esp)
; X32-NEXT:    flds (%esp)
; X32-NEXT:    popl %eax
; X32-NEXT:    retl
;
; X64-LABEL: signbits_ashr_extract_sitofp_0:
; X64:       # BB#0:
; X64-NEXT:    vpsrad $31, %xmm0, %xmm1
; X64-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[1,1,3,3]
; X64-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1],xmm1[2,3],xmm0[4,5],xmm1[6,7]
; X64-NEXT:    vmovq %xmm0, %rax
; X64-NEXT:    vcvtsi2ssl %eax, %xmm2, %xmm0
; X64-NEXT:    retq
  %1 = ashr <2 x i64> %a0, <i64 32, i64 32>
  %2 = extractelement <2 x i64> %1, i32 0
  %3 = sitofp i64 %2 to float
  ret float %3
}

define float @signbits_ashr_extract_sitofp_1(<2 x i64> %a0) nounwind {
; X32-LABEL: signbits_ashr_extract_sitofp_1:
; X32:       # BB#0:
; X32-NEXT:    pushl %ebp
; X32-NEXT:    movl %esp, %ebp
; X32-NEXT:    andl $-8, %esp
; X32-NEXT:    subl $16, %esp
; X32-NEXT:    vmovdqa {{.*#+}} xmm1 = [0,2147483648,0,2147483648]
; X32-NEXT:    vpsrlq $63, %xmm1, %xmm2
; X32-NEXT:    vpsrlq $32, %xmm1, %xmm1
; X32-NEXT:    vpblendw {{.*#+}} xmm1 = xmm1[0,1,2,3],xmm2[4,5,6,7]
; X32-NEXT:    vpsrlq $63, %xmm0, %xmm2
; X32-NEXT:    vpsrlq $32, %xmm0, %xmm0
; X32-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1,2,3],xmm2[4,5,6,7]
; X32-NEXT:    vpxor %xmm1, %xmm0, %xmm0
; X32-NEXT:    vpsubq %xmm1, %xmm0, %xmm0
; X32-NEXT:    vmovq %xmm0, {{[0-9]+}}(%esp)
; X32-NEXT:    fildll {{[0-9]+}}(%esp)
; X32-NEXT:    fstps {{[0-9]+}}(%esp)
; X32-NEXT:    flds {{[0-9]+}}(%esp)
; X32-NEXT:    movl %ebp, %esp
; X32-NEXT:    popl %ebp
; X32-NEXT:    retl
;
; X64-LABEL: signbits_ashr_extract_sitofp_1:
; X64:       # BB#0:
; X64-NEXT:    vpsrlq $63, %xmm0, %xmm1
; X64-NEXT:    vpsrlq $32, %xmm0, %xmm0
; X64-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1,2,3],xmm1[4,5,6,7]
; X64-NEXT:    vmovdqa {{.*#+}} xmm1 = [2147483648,1]
; X64-NEXT:    vpxor %xmm1, %xmm0, %xmm0
; X64-NEXT:    vpsubq %xmm1, %xmm0, %xmm0
; X64-NEXT:    vmovq %xmm0, %rax
; X64-NEXT:    vcvtsi2ssq %rax, %xmm2, %xmm0
; X64-NEXT:    retq
  %1 = ashr <2 x i64> %a0, <i64 32, i64 63>
  %2 = extractelement <2 x i64> %1, i32 0
  %3 = sitofp i64 %2 to float
  ret float %3
}

define float @signbits_ashr_shl_extract_sitofp(<2 x i64> %a0) nounwind {
; X32-LABEL: signbits_ashr_shl_extract_sitofp:
; X32:       # BB#0:
; X32-NEXT:    pushl %ebp
; X32-NEXT:    movl %esp, %ebp
; X32-NEXT:    andl $-8, %esp
; X32-NEXT:    subl $16, %esp
; X32-NEXT:    vmovdqa {{.*#+}} xmm1 = [0,2147483648,0,2147483648]
; X32-NEXT:    vpsrlq $60, %xmm1, %xmm2
; X32-NEXT:    vpsrlq $61, %xmm1, %xmm1
; X32-NEXT:    vpblendw {{.*#+}} xmm1 = xmm1[0,1,2,3],xmm2[4,5,6,7]
; X32-NEXT:    vpsrlq $60, %xmm0, %xmm2
; X32-NEXT:    vpsrlq $61, %xmm0, %xmm0
; X32-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1,2,3],xmm2[4,5,6,7]
; X32-NEXT:    vpxor %xmm1, %xmm0, %xmm0
; X32-NEXT:    vpsubq %xmm1, %xmm0, %xmm0
; X32-NEXT:    vpsllq $16, %xmm0, %xmm1
; X32-NEXT:    vpsllq $20, %xmm0, %xmm0
; X32-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1,2,3],xmm1[4,5,6,7]
; X32-NEXT:    vmovq %xmm0, {{[0-9]+}}(%esp)
; X32-NEXT:    fildll {{[0-9]+}}(%esp)
; X32-NEXT:    fstps {{[0-9]+}}(%esp)
; X32-NEXT:    flds {{[0-9]+}}(%esp)
; X32-NEXT:    movl %ebp, %esp
; X32-NEXT:    popl %ebp
; X32-NEXT:    retl
;
; X64-LABEL: signbits_ashr_shl_extract_sitofp:
; X64:       # BB#0:
; X64-NEXT:    vpsrlq $60, %xmm0, %xmm1
; X64-NEXT:    vpsrlq $61, %xmm0, %xmm0
; X64-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1,2,3],xmm1[4,5,6,7]
; X64-NEXT:    vmovdqa {{.*#+}} xmm1 = [4,8]
; X64-NEXT:    vpxor %xmm1, %xmm0, %xmm0
; X64-NEXT:    vpsubq %xmm1, %xmm0, %xmm0
; X64-NEXT:    vpsllq $20, %xmm0, %xmm0
; X64-NEXT:    vmovq %xmm0, %rax
; X64-NEXT:    vcvtsi2ssq %rax, %xmm2, %xmm0
; X64-NEXT:    retq
  %1 = ashr <2 x i64> %a0, <i64 61, i64 60>
  %2 = shl <2 x i64> %1, <i64 20, i64 16>
  %3 = extractelement <2 x i64> %2, i32 0
  %4 = sitofp i64 %3 to float
  ret float %4
}

define float @signbits_ashr_insert_ashr_extract_sitofp(i64 %a0, i64 %a1) nounwind {
; X32-LABEL: signbits_ashr_insert_ashr_extract_sitofp:
; X32:       # BB#0:
; X32-NEXT:    pushl %eax
; X32-NEXT:    movl {{[0-9]+}}(%esp), %eax
; X32-NEXT:    movl {{[0-9]+}}(%esp), %ecx
; X32-NEXT:    shrdl $30, %ecx, %eax
; X32-NEXT:    sarl $30, %ecx
; X32-NEXT:    vmovd %eax, %xmm0
; X32-NEXT:    vpinsrd $1, %ecx, %xmm0, %xmm0
; X32-NEXT:    vpinsrd $2, {{[0-9]+}}(%esp), %xmm0, %xmm0
; X32-NEXT:    vpinsrd $3, {{[0-9]+}}(%esp), %xmm0, %xmm0
; X32-NEXT:    vpsrlq $3, %xmm0, %xmm0
; X32-NEXT:    vmovd %xmm0, %eax
; X32-NEXT:    vcvtsi2ssl %eax, %xmm1, %xmm0
; X32-NEXT:    vmovss %xmm0, (%esp)
; X32-NEXT:    flds (%esp)
; X32-NEXT:    popl %eax
; X32-NEXT:    retl
;
; X64-LABEL: signbits_ashr_insert_ashr_extract_sitofp:
; X64:       # BB#0:
; X64-NEXT:    sarq $30, %rdi
; X64-NEXT:    vmovq %rsi, %xmm0
; X64-NEXT:    vmovq %rdi, %xmm1
; X64-NEXT:    vpunpcklqdq {{.*#+}} xmm0 = xmm1[0],xmm0[0]
; X64-NEXT:    vpsrad $3, %xmm0, %xmm1
; X64-NEXT:    vpsrlq $3, %xmm0, %xmm0
; X64-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1],xmm1[2,3],xmm0[4,5],xmm1[6,7]
; X64-NEXT:    vmovq %xmm0, %rax
; X64-NEXT:    vcvtsi2ssl %eax, %xmm2, %xmm0
; X64-NEXT:    retq
  %1 = ashr i64 %a0, 30
  %2 = insertelement <2 x i64> undef, i64 %1, i32 0
  %3 = insertelement <2 x i64> %2, i64 %a1, i32 1
  %4 = ashr <2 x i64> %3, <i64 3, i64 3>
  %5 = extractelement <2 x i64> %4, i32 0
  %6 = sitofp i64 %5 to float
  ret float %6
}

define <4 x double> @signbits_sext_shuffle_sitofp(<4 x i32> %a0, <4 x i64> %a1) nounwind {
; X32-LABEL: signbits_sext_shuffle_sitofp:
; X32:       # BB#0:
; X32-NEXT:    vpmovsxdq %xmm0, %xmm1
; X32-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; X32-NEXT:    vpmovsxdq %xmm0, %xmm0
; X32-NEXT:    vinsertf128 $1, %xmm0, %ymm1, %ymm0
; X32-NEXT:    vpermilpd {{.*#+}} ymm0 = ymm0[1,0,3,2]
; X32-NEXT:    vperm2f128 {{.*#+}} ymm0 = ymm0[2,3,0,1]
; X32-NEXT:    vextractf128 $1, %ymm0, %xmm1
; X32-NEXT:    vshufps {{.*#+}} xmm0 = xmm0[0,2],xmm1[0,2]
; X32-NEXT:    vcvtdq2pd %xmm0, %ymm0
; X32-NEXT:    retl
;
; X64-LABEL: signbits_sext_shuffle_sitofp:
; X64:       # BB#0:
; X64-NEXT:    vpmovsxdq %xmm0, %xmm1
; X64-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; X64-NEXT:    vpmovsxdq %xmm0, %xmm0
; X64-NEXT:    vinsertf128 $1, %xmm0, %ymm1, %ymm0
; X64-NEXT:    vpermilpd {{.*#+}} ymm0 = ymm0[1,0,3,2]
; X64-NEXT:    vperm2f128 {{.*#+}} ymm0 = ymm0[2,3,0,1]
; X64-NEXT:    vextractf128 $1, %ymm0, %xmm1
; X64-NEXT:    vshufps {{.*#+}} xmm0 = xmm0[0,2],xmm1[0,2]
; X64-NEXT:    vcvtdq2pd %xmm0, %ymm0
; X64-NEXT:    retq
  %1 = sext <4 x i32> %a0 to <4 x i64>
  %2 = shufflevector <4 x i64> %1, <4 x i64>%a1, <4 x i32> <i32 3, i32 2, i32 1, i32 0>
  %3 = sitofp <4 x i64> %2 to <4 x double>
  ret <4 x double> %3
}

define <2 x double> @signbits_ashr_concat_ashr_extract_sitofp(<2 x i64> %a0, <4 x i64> %a1) nounwind {
; X32-LABEL: signbits_ashr_concat_ashr_extract_sitofp:
; X32:       # BB#0:
; X32-NEXT:    vpsrad $16, %xmm0, %xmm1
; X32-NEXT:    vpsrlq $16, %xmm0, %xmm0
; X32-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1],xmm1[2,3],xmm0[4,5],xmm1[6,7]
; X32-NEXT:    vpsrlq $16, %xmm0, %xmm0
; X32-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; X32-NEXT:    vcvtdq2pd %xmm0, %xmm0
; X32-NEXT:    retl
;
; X64-LABEL: signbits_ashr_concat_ashr_extract_sitofp:
; X64:       # BB#0:
; X64-NEXT:    vpsrad $16, %xmm0, %xmm1
; X64-NEXT:    vpsrlq $16, %xmm0, %xmm0
; X64-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1],xmm1[2,3],xmm0[4,5],xmm1[6,7]
; X64-NEXT:    vpsrlq $16, %xmm0, %xmm0
; X64-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; X64-NEXT:    vcvtdq2pd %xmm0, %xmm0
; X64-NEXT:    retq
  %1 = ashr <2 x i64> %a0, <i64 16, i64 16>
  %2 = shufflevector <2 x i64> %1, <2 x i64> undef, <4 x i32> <i32 0, i32 1, i32 undef, i32 undef>
  %3 = shufflevector <4 x i64> %a1, <4 x i64> %2, <4 x i32> <i32 0, i32 1, i32 4, i32 5>
  %4 = ashr <4 x i64> %3, <i64 16, i64 16, i64 16, i64 16>
  %5 = shufflevector <4 x i64> %4, <4 x i64> undef, <2 x i32> <i32 2, i32 3>
  %6 = sitofp <2 x i64> %5 to <2 x double>
  ret <2 x double> %6
}
