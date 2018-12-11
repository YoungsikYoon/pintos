#include "vm/swap.h"

static struct block* swap_block;
static struct bitmap* swap_bitmap;

void vm_swap_init(){
  swap_block = block_get_role(BLOCK_SWAP); 
  swap_bitmap = bitmap_create(1024);

  bitmap_set_all(swap_bitmap, true);
}

uint32_t vm_swap_out(void* page){
  size_t sector_index = bitmap_scan(swap_bitmap, 0, 1, true);
  size_t i;

  for(i = 0; i < 8; i++){
    block_write(swap_block, sector_index * 8 + i, page + (512 * i));
  }
  bitmap_set(swap_bitmap, sector_index, false);

  return sector_index;
}

void vm_swap_in (uint32_t sector_index, void* page){
  size_t i;
  
  for(i = 0; i < 8; ++i){
    block_read(swap_block, sector_index * 8 + i, page + (512 * i));
  }

  vm_swap_free(sector_index);
}

void vm_swap_free (uint32_t sector_index){
  bitmap_set(swap_bitmap, sector_index, true);
}
