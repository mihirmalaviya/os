bits 64
section .text
global gdt_load
global tss_load
global idt_load

; void idt_load(idtr_t *idtr)
idt_load:
    lidt [rdi]
    ret

gdt_load:
    lgdt  [rdi]         ; rdi = pointer to gdtr struct

    push  0x08
    lea   rax, [rel .reload_CS]
    push  rax
    retfq

.reload_CS:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

tss_load:
    mov ax, 0x28        ; GDT_TSS selector
    ltr ax
    ret
