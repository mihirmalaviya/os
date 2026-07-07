bits 64
section .text
global read_cr3
global write_cr3
global invlpg

read_cr3:
    mov rax, cr3
    ret

write_cr3:
    mov cr3, rdi
    ret

invlpg:
    invlpg [rdi]
    ret
