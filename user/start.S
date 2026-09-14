.section .text.start
.global _start
.type _start, @function
_start:
    xor %ebp, %ebp
    call main
    mov %eax, %edi
    mov $2, %eax
    int $0x80
    ud2
.size _start, .-_start
.section .note.GNU-stack,"",@progbits
