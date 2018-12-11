#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <bitmap.h>
#include "threads/vaddr.h"
#include "devices/block.h"
#include "vm/swap.h"

void vm_swap_init();
uint32_t vm_swap_out(void* page);
void vm_swap_in(uint32_t sector_index, void* page);
void vm_swap_free(uint32_t sector_index);

#endif
