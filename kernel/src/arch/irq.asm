bits 64
section .text
global irq_save
global irq_restore
global cli
global sti

; uint64_t irq_save(void)
; save the current interrupt state (rflags) then disable interrupts. returns
; the old flags so irq_restore can put them back exactly as they were. safe to
; call from task or isr context: restoring leaves interrupts off if they were
; already off.
irq_save:
    pushfq
    pop rax
    cli
    ret

; void irq_restore(uint64_t flags)
irq_restore:
    push rdi
    popfq
    ret

; void cli(void) - disable interrupts
cli:
    cli
    ret

; void sti(void) - enable interrupts
sti:
    sti
    ret
