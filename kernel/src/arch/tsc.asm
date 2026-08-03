bits 64
section .text
global rdtsc

; uint64_t rdtsc(void)
rdtsc:
    rdtsc
    shl rdx, 32
    or rax, rdx
    ret
