#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <string.h>
#include "lib/kernel/hash.h"
#include "filesys/off_t.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"

enum page_type{
  FRAME,
  ZERO,
  SWAP,
  FILE_SYS
};

struct spage_table{
  struct hash page_hash;
};

struct spage{
  void* upage;
  void* kpage;

  struct hash_elem elem;

  enum page_type type;

  bool dirty;

  uint32_t sector_index;

  struct file* file;
  off_t offset;
  uint32_t read_bytes, zero_bytes;
  bool writable;
};

struct spage_table* vm_spage_table_create (void);
void vm_spage_table_destroy (struct spage_table* spt);

void vm_spage_table_install(struct spage_table* spt, enum page_type type, 
		void* upage, void* kpage, uint32_t sector_index, struct file* file, 
		off_t offset, uint32_t read_bytes, uint32_t zero_bytes, bool writable);

struct spage* vm_find_spage (struct spage_table* spt, void* upage);
bool vm_load_page(struct spage_table* spt, uint32_t* pagedir, void* upage);

void vm_spage_table_mm_unmap(struct spage_table* spt, uint32_t* pagedir, void* page, struct file* f, off_t offset, size_t bytes);

#endif
