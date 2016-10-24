; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: llc < %s -mtriple=x86_64-unknown-unknown | FileCheck %s

define i32 @select_0_or_1(i1 %cond) {
; CHECK-LABEL: select_0_or_1:
; CHECK:       # BB#0:
; CHECK-NEXT:    notb %dil
; CHECK-NEXT:    movzbl %dil, %eax
; CHECK-NEXT:    andl $1, %eax
; CHECK-NEXT:    retq
;
  %sel = select i1 %cond, i32 0, i32 1
  ret i32 %sel
}

define i32 @select_0_or_1_zeroext(i1 zeroext %cond) {
; CHECK-LABEL: select_0_or_1_zeroext:
; CHECK:       # BB#0:
; CHECK-NEXT:    xorb $1, %dil
; CHECK-NEXT:    movzbl %dil, %eax
; CHECK-NEXT:    retq
;
  %sel = select i1 %cond, i32 0, i32 1
  ret i32 %sel
}

define i32 @select_1_or_0(i1 %cond) {
; CHECK-LABEL: select_1_or_0:
; CHECK:       # BB#0:
; CHECK-NEXT:    andl $1, %edi
; CHECK-NEXT:    movl %edi, %eax
; CHECK-NEXT:    retq
;
  %sel = select i1 %cond, i32 1, i32 0
  ret i32 %sel
}

define i32 @select_1_or_0_zeroext(i1 zeroext %cond) {
; CHECK-LABEL: select_1_or_0_zeroext:
; CHECK:       # BB#0:
; CHECK-NEXT:    movzbl %dil, %eax
; CHECK-NEXT:    retq
;
  %sel = select i1 %cond, i32 1, i32 0
  ret i32 %sel
}

define i32 @select_0_or_neg1(i1 %cond) {
; CHECK-LABEL: select_0_or_neg1:
; CHECK:       # BB#0:
; CHECK-NEXT:    # kill: %EDI<def> %EDI<kill> %RDI<def>
; CHECK-NEXT:    andl $1, %edi
; CHECK-NEXT:    leal -1(%rdi), %eax
; CHECK-NEXT:    retq
;
  %sel = select i1 %cond, i32 0, i32 -1
  ret i32 %sel
}

define i32 @select_0_or_neg1_zeroext(i1 zeroext %cond) {
; CHECK-LABEL: select_0_or_neg1_zeroext:
; CHECK:       # BB#0:
; CHECK-NEXT:    movzbl %dil, %eax
; CHECK-NEXT:    decl %eax
; CHECK-NEXT:    retq
;
  %sel = select i1 %cond, i32 0, i32 -1
  ret i32 %sel
}

define i32 @select_neg1_or_0(i1 %cond) {
; CHECK-LABEL: select_neg1_or_0:
; CHECK:       # BB#0:
; CHECK-NEXT:    xorl %ecx, %ecx
; CHECK-NEXT:    testb $1, %dil
; CHECK-NEXT:    movl $-1, %eax
; CHECK-NEXT:    cmovel %ecx, %eax
; CHECK-NEXT:    retq
;
  %sel = select i1 %cond, i32 -1, i32 0
  ret i32 %sel
}

define i32 @select_neg1_or_0_zeroext(i1 zeroext %cond) {
; CHECK-LABEL: select_neg1_or_0_zeroext:
; CHECK:       # BB#0:
; CHECK-NEXT:    xorl %ecx, %ecx
; CHECK-NEXT:    testb %dil, %dil
; CHECK-NEXT:    movl $-1, %eax
; CHECK-NEXT:    cmovel %ecx, %eax
; CHECK-NEXT:    retq
;
  %sel = select i1 %cond, i32 -1, i32 0
  ret i32 %sel
}

define i64 @select_2_or_inc(i64 %x) {
; CHECK-LABEL: select_2_or_inc:
; CHECK:       # BB#0:
; CHECK-NEXT:    leaq 1(%rdi), %rax
; CHECK-NEXT:    cmpq $2, %rdi
; CHECK-NEXT:    cmoveq %rdi, %rax
; CHECK-NEXT:    retq
;
  %cmp = icmp eq i64 %x, 2
  %add = add i64 %x, 1
  %retval.0 = select i1 %cmp, i64 2, i64 %add
  ret i64 %retval.0
}

