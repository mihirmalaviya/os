bits 64
section .text

extern isr_handler

%macro ISR_NOERRORCODE 1
isr_stub_%1:
    push 0
    push %1
    jmp isr_common
%endmacro

%macro ISR_ERRORCODE 1
isr_stub_%1:
    push %1
    jmp isr_common
%endmacro

%assign i 0
%rep 256
    %if i = 8 || i = 10 || i = 11 || i = 12 || i = 13 || i = 14 || i = 17 || i = 21 || i = 29 || i = 30
        ISR_ERRORCODE i
    %else
        ISR_NOERRORCODE i
    %endif
    %assign i i+1
%endrep

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call isr_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    iretq

global isr_stub_table
isr_stub_table:
%assign i 0
%rep 256
    dq isr_stub_%+i
    %assign i i+1
%endrep
