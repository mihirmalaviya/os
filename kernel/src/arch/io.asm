bits 64
section .text
global inb
global outb
global inw
global outw
global inl
global outl

inb:
    mov dx, di
    in al, dx
    ret

outb:
    mov dx, di
    mov ax, si
    out dx, al
    ret

inw:
    mov dx, di
    in ax, dx
    ret

outw:
    mov dx, di
    mov ax, si
    out dx, ax
    ret

inl:
    mov dx, di
    in eax, dx
    ret

outl:
    mov dx, di
    mov eax, esi
    out dx, eax
    ret
