    .text

    .globl __restore_rt
    .hidden __restore_rt
    .type __restore_rt, @function
__restore_rt:
    movq $15, %rax
    syscall
    ud2
    .size __restore_rt, .-__restore_rt

    .globl clone
    .type clone, @function
clone:
    testq %rdi, %rdi
    je .Lclone_invalid
    testq %rsi, %rsi
    je .Lclone_invalid

    andq $-16, %rsi
    subq $16, %rsi
    movq %rdi, 0(%rsi)
    movq %rcx, 8(%rsi)

    movq %rdx, %rdi
    movq %r8, %rdx
    movq 8(%rsp), %r10
    movq %r9, %r8
    movq $56, %rax
    syscall

    testq %rax, %rax
    jne .Lclone_parent

    popq %rax
    popq %rdi
    call *%rax
    movl %eax, %edi
    call thread_exit
    ud2

.Lclone_parent:
    movq %rax, %rdi
    jmp __punos_syscall_result

.Lclone_invalid:
    movq $-22, %rdi
    jmp __punos_syscall_result
    .size clone, .-clone

    .section .note.GNU-stack,"",@progbits
