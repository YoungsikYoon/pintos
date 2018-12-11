#include "vm/frame.h"

static struct lock frame_lock;
static struct hash frame_hash;
static struct list frame_list;

struct list_elem* before;

struct frame* next_candi(void){
  if(before == NULL) before = list_begin(&frame_list);
  else if(before == list_end(&frame_list)) before = list_begin(&frame_list);
  else before = list_next(before);

  return list_entry(before, struct frame, elem_);
}

void evict_frame(uint32_t* pagedir) {
  struct frame* f = next_candi();
  while(1){
    if(pagedir_is_accessed(pagedir, f->upage)) pagedir_set_accessed(pagedir, f->upage, false);
    else if(!(f->pinned)) break;
    f = next_candi();
  }
  pagedir_clear_page(f->t->pagedir, f->upage);
  vm_spage_table_install(f->t->spt, SWAP, f->upage, NULL, vm_swap_out(f->kpage), NULL, 0, 0, 0, false);
  
  if(pagedir_is_dirty(f->t->pagedir, f->upage)||pagedir_is_dirty(f->t->pagedir, f->kpage))
    vm_find_spage(f->t->spt, f->upage)->dirty = true;

  vm_frame_deallocate(f->kpage, true);
}

static unsigned hash_func(const struct hash_elem* elem, void* aux) {
  struct frame *a = hash_entry(elem, struct frame, elem);
  return hash_bytes(&a->kpage, sizeof a->kpage); 
}

static bool less_func(const struct hash_elem* a, const struct hash_elem* b, void* aux) {
  struct frame *a_ = hash_entry(a, struct frame, elem);
  struct frame *b_ = hash_entry(b, struct frame, elem);
  return a_->kpage < b_->kpage;
}

void vm_frame_init() {
  lock_init(&frame_lock);
  hash_init(&frame_hash, hash_func, less_func, NULL);
  list_init(&frame_list);
  before = NULL;
}

void* vm_frame_allocate(void* upage) {
  lock_acquire(&frame_lock); 
  void* fpage = palloc_get_page(PAL_USER);

  if(fpage == NULL){
    evict_frame(thread_current()->pagedir);
    fpage = palloc_get_page(PAL_USER);
  }

  struct frame* f = malloc(sizeof(struct frame));

  if(f == NULL){
    lock_release(&frame_lock);
    return NULL;
  }

  f->t = thread_current();
  f->upage = upage;
  f->kpage = fpage;
  f->pinned = true;

  hash_insert(&frame_hash, &f->elem);
  list_push_back(&frame_list, &f->elem_);

  lock_release(&frame_lock);
  return fpage;
}

void vm_frame_deallocate(void *kpage, bool freep){
  bool lock = false;

  if(lock_held_by_current_thread(&frame_lock)) lock = true;
  if(lock == false) lock_acquire(&frame_lock);
  
  struct frame temp;

  temp.kpage = kpage;

  struct hash_elem *h = hash_find (&frame_hash, &(temp.elem));

  struct frame* f = hash_entry(h, struct frame, elem);

  hash_delete(&frame_hash, &f->elem);
  list_remove(&f->elem_);

  if(freep)palloc_free_page(kpage);
  free(f);
  if(lock == false) lock_release(&frame_lock);
}

void vm_frame_pinning(void* kpage){
  lock_acquire(&frame_lock);

  struct frame temp;
  temp.kpage = kpage;
  struct hash_elem* h = hash_find(&frame_hash, &(temp.elem));

  struct frame* f = hash_entry(h, struct frame, elem);
  f->pinned = true;

  lock_release(&frame_lock);
}

void vm_frame_unpinning(void* kpage){
  lock_acquire(&frame_lock);

  struct frame temp;
  temp.kpage = kpage;
  struct hash_elem* h = hash_find(&frame_hash, &(temp.elem));

  struct frame* f = hash_entry(h, struct frame, elem);
  f->pinned = false;
  
  lock_release(&frame_lock);
}
