bits 64
section .text

extern current_tcb        ; thread_control_block_t*
extern tss_rsp0_ptr       ; uint64_t* pointing at tss.rsp0
global context_switch

; C declaration:
;   void context_switch(thread_control_block_t *next);   ; next arrives in rdi (System V ABI)
;
; WARNING: caller must disable IRQs before calling and re-enable after it returns

context_switch:
    ; Save outgoing task's callee-saved registers.
    ; System V ABI callee-saved set: rbx, rbp, r12-r15
    ; (rax/rcx/rdx/rsi/rdi/r8-r11 are caller-saved, already handled by the caller
    ;  before this CALL; the return RIP is already on the stack from CALL itself)
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov rax, [current_tcb]         ; rax = address of the previous task's tcb
    mov [rax], rsp                 ; overwrite the first part of the struct with
                                   ; rsp (points at r15) so we have it ready
                                   ; for when we wanna pop all of them back

    mov [current_tcb], rdi         ; rdi contains next tcb, so now current tcb is next tcb

    mov rsp, [rdi]                 ; [rdi] is the first part of the next tcb's struct -> rsp
					                         ; now the stack points there

    mov rax, cr3                   ; move cr3 (pointer to page table) to rax
    mov rcx, [rdi + 16]            ; move next tcb's cr3 to rcx
    cmp rax, rcx									 ; same? do nothing, else switch
    je .done_vas
    mov cr3, rcx                   ; only reload cr3 if it actually changed (avoid TLB flush)
.done_vas:

    mov rax, [rdi + 8]              ; next->rsp0
    mov rcx, [rel tss_rsp0_ptr]    ; address of tss.rsp0 field
    mov [rcx], rax                  ; tss.rsp0 = next->rsp0

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    ret                             ; pops next task's saved RIP off its stack
