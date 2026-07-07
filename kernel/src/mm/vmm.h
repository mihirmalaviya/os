#pragma once
#include <stdint.h>

#define VMM_PRESENT   (1ULL << 0)
#define VMM_WRITABLE  (1ULL << 1)
#define VMM_USER      (1ULL << 2)

uint64_t read_cr3(void);
void     write_cr3(uint64_t phys);
void     invlpg(uint64_t virt);
uint64_t *vmm_new_pagemap(void);
void      vmm_map(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
void      vmm_unmap(uint64_t *pml4, uint64_t virt);
void      vmm_switch(uint64_t *pml4);
uint64_t *vmm_get_current_pml4(void);
void      vmm_init(void);
