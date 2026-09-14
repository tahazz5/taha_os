.section .text
.global kentry
.type kentry, @function
kentry:
    cli
    cld
    lea kernel_stack_top(%rip), %rsp
    xor %ebp, %ebp
    call kmain
1:  cli
    hlt
    jmp 1b
.size kentry, .-kentry

.global load_gdt
.type load_gdt, @function
load_gdt:
    lgdt (%rdi)
    pushq $0x08
    lea 1f(%rip), %rax
    pushq %rax
    lretq
1:  mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    xor %eax, %eax
    mov %ax, %fs
    mov %ax, %gs
    mov $0x18, %ax
    ltr %ax
    ret
.size load_gdt, .-load_gdt

// Enter a real stack-overflow path: #PF cannot push its frame, so #DF
// must switch to IST1. This diagnostic never returns.
.global trigger_stack_fault
.type trigger_stack_fault, @function
trigger_stack_fault:
    cli
    lea kernel_stack_guard+4096(%rip), %rsp
    pushq $0
    ud2
.size trigger_stack_fault, .-trigger_stack_fault


.global test_interrupt_registers
.type test_interrupt_registers, @function
test_interrupt_registers:
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15
    mov $256, %rax
    mov $257, %rbx
    mov $258, %rcx
    mov $259, %rdx
    mov $260, %rsi
    mov $261, %rdi
    mov $262, %rbp
    mov $263, %r8
    mov $264, %r9
    mov $265, %r10
    mov $266, %r11
    mov $267, %r12
    mov $268, %r13
    mov $269, %r14
    mov $270, %r15
    std
    stc
    int3
    pushfq
    cld
    cmp $256, %rax
    jne 1f
    cmp $257, %rbx
    jne 1f
    cmp $258, %rcx
    jne 1f
    cmp $259, %rdx
    jne 1f
    cmp $260, %rsi
    jne 1f
    cmp $261, %rdi
    jne 1f
    cmp $262, %rbp
    jne 1f
    cmp $263, %r8
    jne 1f
    cmp $264, %r9
    jne 1f
    cmp $265, %r10
    jne 1f
    cmp $266, %r11
    jne 1f
    cmp $267, %r12
    jne 1f
    cmp $268, %r13
    jne 1f
    cmp $269, %r14
    jne 1f
    cmp $270, %r15
    jne 1f
    mov (%rsp), %rax
    and $0x401, %eax
    cmp $0x401, %eax
    jne 1f
    mov $1, %eax
    jmp 2f
1:  xor %eax, %eax
2:  add $8, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    ret
.size test_interrupt_registers, .-test_interrupt_registers

.altmacro
.macro ISR number
.global isr_\number
isr_\number:
.if !(\number == 8 || \number == 10 || \number == 11 || \number == 12 || \number == 13 || \number == 14 || \number == 17 || \number == 21 || \number == 29 || \number == 30)
    pushq $0
.endif
    pushq $\number
    jmp interrupt_common
.endm
.set vector, 0
.rept 256
    ISR %vector
    .set vector, vector+1
.endr

interrupt_common:
    cld
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, %rdi
    mov %rsp, %rbx
    and $-16, %rsp
    call interrupt_dispatch
    mov %rbx, %rsp
restore_frame:
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    add $16, %rsp
    iretq

.global resume_user
.type resume_user, @function
resume_user:
    cli
    mov $0x33, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %rdi, %rsp
    jmp restore_frame
.size resume_user, .-resume_user

.section .rodata
.global isr_table
isr_table:
.macro ISR_POINTER number
    .quad isr_\number
.endm
.set vector, 0
.rept 256
    ISR_POINTER %vector
    .set vector, vector+1
.endr

.section .bss
.balign 4096
.global kernel_stack_guard, kernel_stack_top
kernel_stack_guard:
    .skip 4096
    .skip 65536
kernel_stack_top:

.macro EXCEPTION_STACK name
.balign 4096
.global \name\()_stack_guard, \name\()_stack_top
\name\()_stack_guard:
    .skip 4096
    .skip 16384
\name\()_stack_top:
.endm
EXCEPTION_STACK df
EXCEPTION_STACK nmi
EXCEPTION_STACK mc
.section .note.GNU-stack,"",@progbits
