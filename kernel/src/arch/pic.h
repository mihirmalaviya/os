#pragma once
#include <stdint.h>

void pic_init(void);
void PIC_sendEOI(uint8_t irq);
void IRQ_set_mask(uint8_t IRQline);
void IRQ_clear_mask(uint8_t IRQline);
