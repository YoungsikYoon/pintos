#include "vm/page.h"

static unsigned hash_func(const struct hash_elem* elem, void* aux){
  struct spage* s = hash_entry(elem, struct spage, elem);
  return hash_int((int)s->upage);
}

static bool less_func(const struct hash_elem* x, const struct hash_elem* y, void* aux){
  struct spage *x_ = hash_entry(x, struct spage, elem);
  struct spage *y_ = hash_entry(y, struct spage, elem);
  return x_->upage < y_->upage;
}

static void destroy_func(struct hash_elem* elem, void* aux){
  struct spage *s = hash_entry(elem, struct spage, elem);

  if(s->kpage != NULL) vm_frame_deallocate(s->kpage, false);
  else if(s->type == SWAP) vm_swap_free (s->sector_index);

  free(s);
}

struct spage_table* vm_spage_table_create(){
  struct spage_table* spt = (struct spage_table*) malloc(sizeof(struct spage_table));

  hash_init(&spt->page_hash, hash_func, less_func, NULL);
  return spt;
}

void vm_spage_table_destroy(struct spage_table *spt){
  hash_destroy (&spt->page_hash, destroy_func);
  free(spt);
}

void vm_spage_table_install(struct spage_table* spt, enum page_type type,
		void* upage, void* kpage, uint32_t sector_index, struct file* file,
		off_t offset, uint32_t read_bytes, uint32_t zero_bytes, bool writable){
  
  struct spage* sp;
  if(type == SWAP) sp = vm_find_spage(spt, upage);
  else sp = (struct spage*)malloc(sizeof(struct spage));

  sp->type = type;
  sp->kpage = kpage;
  sp->sector_index = sector_index;
  if(type!=SWAP){
    sp->upage = upage;
    sp->file = file;
    sp->offset = offset;
    sp->read_bytes = read_bytes;
    sp->zero_bytes = zero_bytes;
    sp->writable = writable;
    sp->dirty = NULL;
    hash_insert(&spt->page_hash, &sp->elem);
  }
}

struct spage* vm_find_spage(struct spage_table* spt, void* upage){
  struct spage temp;
  temp.upage = upage;

  struct hash_elem* elem = hash_find (&spt->page_hash, &temp.elem);
  if(elem == NULL) return NULL;
  else return hash_entry(elem, struct spage, elem);
}

bool vm_load_page(struct spage_table* spt, uint32_t* pagedir, void* upage){
  struct spage* sp = vm_find_spage(spt, upage);

  if(sp == NULL) return false;
  if(sp->type == FRAME) return true;

  void* fpage = vm_frame_allocate(upage);
  
  if(fpage == NULL) return false;

  switch(sp->type){
    case ZERO:
      memset(fpage, 0, PGSIZE);
      pagedir_set_page(pagedir, upage, fpage, true);
      break;

    case FRAME:
      break;

    case SWAP:
      vm_swap_in(sp->sector_index, fpage);
      pagedir_set_page(pagedir, upage, fpage, true);
      break;

    case FILE_SYS:
      file_seek(sp->file, sp->offset);
      memset(fpage + file_read(sp->file,fpage,sp->read_bytes), 0, sp->zero_bytes);
      pagedir_set_page(pagedir, upage, fpage, sp->writable);
      break;
  }
  sp->kpage = fpage;
  sp->type = FRAME;

  pagedir_set_dirty(pagedir, fpage, false);

  vm_frame_unpinning(fpage);
  
  return true;
}

void vm_spage_table_mm_unmap(struct spage_table* spt, uint32_t* pagedir, void* page, struct file* f, off_t offset, size_t bytes){ 
  struct spage* sp = vm_find_spage(spt, page);

  if(sp->type == FRAME){
    vm_frame_pinning(sp->kpage);
  }

  switch(sp->type){
    case ZERO:
      break;

    case FRAME:
      if(sp->dirty || pagedir_is_dirty(pagedir, sp->upage) || pagedir_is_dirty(pagedir, sp->kpage))
	file_write_at (f, sp->upage, bytes, offset);
      vm_frame_deallocate(sp->kpage, true);
      pagedir_clear_page(pagedir, sp->upage);
      break;

    case SWAP:
      if(sp->dirty || pagedir_is_dirty(pagedir, sp->upage)) {
        void* temp = palloc_get_page(0);
	vm_swap_in(sp->sector_index, temp);
	file_write_at(f, temp, PGSIZE, offset);
	palloc_free_page(temp);
      }
      else vm_swap_free(sp->sector_index);

      break;

    case FILE_SYS:
      break;
  }

  hash_delete(&spt->page_hash, &sp->elem);
}
