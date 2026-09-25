    .equ CRT_EXIT_TLS_INITIALIZATION_FAILED, 127
    .equ THREAD_STATE_ERRNO_OFFSET, 8
    .equ THREAD_STATE_RESERVED_OFFSET, 12

    .text
    .globl _start
    .type _start, @function
_start:
    xorl %ebp, %ebp

    # Preserve the Linux process-entry arguments in callee-saved registers.
    movq (%rsp), %r12
    leaq 8(%rsp), %r13
    leaq 16(%rsp,%r12,8), %r14
    andq $-16, %rsp

    # Initialize the main thread state before installing it as FS base.
    leaq punos_main_thread_state(%rip), %rdi
    movq %rdi, (%rdi)
    movl $0, THREAD_STATE_ERRNO_OFFSET(%rdi)
    movl $0, THREAD_STATE_RESERVED_OFFSET(%rdi)
    call __punos_raw_arch_set_fs
    testq %rax, %rax
    jne .Ltls_failed

    movl %r12d, %edi
    movq %r13, %rsi
    movq %r14, %rdx
    call crt_start
    ud2

.Ltls_failed:
    movl $CRT_EXIT_TLS_INITIALIZATION_FAILED, %edi
    call __punos_raw_exit_group
    ud2
    .size _start, .-_start

    .section .note.GNU-stack,"",@progbits
