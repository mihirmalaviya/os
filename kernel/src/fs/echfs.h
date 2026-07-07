#pragma once
#include <stdint.h>

// parses the identity table on block 0 and locates the allocation table / main directory
void echfs_mount(uint8_t drive);
