#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <hash.h>
#include <list.h>
#include "lib/kernel/hash.h"
#include "lib/kernel/list.h"

#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/page.h"

// Entry of Frame Table
struct frame {
  void* kpage;
  void* upage;

  struct hash_elem elem;
  struct list_elem elem_;

  struct thread* t;

  bool pinned;
};

// Frame Manipulate Functions

void vm_frame_init(void);
void* vm_frame_allocate(void* upage);
void vm_frame_deallocate(void* kpage, bool freep);
void vm_frame_remove_entry(void* kpage);
void vm_frame_pinning(void* kpage);
void vm_frame_unpinning(void* kpage);


#endif
