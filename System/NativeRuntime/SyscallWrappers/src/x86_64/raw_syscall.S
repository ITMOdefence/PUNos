    .text

    .globl __punos_syscall0
    .type __punos_syscall0, @function
__punos_syscall0:
    movq %rdi, %rax
    syscall
    ret
    .size __punos_syscall0, .-__punos_syscall0

    .globl __punos_syscall1
    .type __punos_syscall1, @function
__punos_syscall1:
    movq %rdi, %rax
    movq %rsi, %rdi
    syscall
    ret
    .size __punos_syscall1, .-__punos_syscall1

    .globl __punos_syscall2
    .type __punos_syscall2, @function
__punos_syscall2:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    syscall
    ret
    .size __punos_syscall2, .-__punos_syscall2

    .globl __punos_syscall3
    .type __punos_syscall3, @function
__punos_syscall3:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    syscall
    ret
    .size __punos_syscall3, .-__punos_syscall3

    .globl __punos_syscall4
    .type __punos_syscall4, @function
__punos_syscall4:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8, %r10
    syscall
    ret
    .size __punos_syscall4, .-__punos_syscall4

    .globl __punos_syscall5
    .type __punos_syscall5, @function
__punos_syscall5:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8, %r10
    movq %r9, %r8
    syscall
    ret
    .size __punos_syscall5, .-__punos_syscall5

    .globl __punos_syscall6
    .type __punos_syscall6, @function
__punos_syscall6:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8, %r10
    movq %r9, %r8
    movq 8(%rsp), %r9
    syscall
    ret
    .size __punos_syscall6, .-__punos_syscall6

    .section .note.GNU-stack,"",@progbits
