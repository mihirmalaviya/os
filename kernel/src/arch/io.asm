bits 64
section .text
global inb
global outb
global inw
global outw

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
