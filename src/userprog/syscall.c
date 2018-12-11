#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "vm/page.h"

static void syscall_handler (struct intr_frame *);

static int get_user(const uint8_t *uaddr);
static bool put_user(uint8_t *udst, uint8_t byte);
int memread(void *src, void *dst, size_t bytes);
static struct file_descriptor* find_fd(int fd);

void exit(int status);
struct lock file_lock;

static struct mmap_descriptor* find_md(int mid);
int sys_mmap(int fd, void *upage);
void sys_munmap(int mid);

void
syscall_init (void) 
{
  lock_init(&file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int sys_num;

  memread(f->esp, &sys_num, sizeof(sys_num)); 

  thread_current()->esp = f->esp;
  
  switch (sys_num){
  
  case SYS_HALT:
  {
    shutdown_power_off();
    
    break;
  }

  case SYS_EXIT:
  {  
    int status;
    
    memread(f->esp+4,&status, sizeof(status));
    
    exit(status);
    
    break;
  }
  case SYS_EXEC:
  {
    void* cmd_line;
    
    memread(f->esp+4, &cmd_line, sizeof(cmd_line));
 
    if(get_user((const uint8_t*)cmd_line) == -1) exit(-1);
    if(get_user((const uint8_t*)cmd_line + sizeof(*cmd_line)) == -1) exit(-1);
    
    lock_acquire(&file_lock);
    f->eax = process_execute((const char*)cmd_line);
    lock_release(&file_lock);
    
    break;
  }
  case SYS_WAIT:
  {
    int pid;
    
    memread(f->esp+4, &pid, sizeof(pid));

    f->eax = process_wait(pid);
    
    break;
  }
  case SYS_CREATE:
  {
    const char* file;
    unsigned initial_size;

    memread(f->esp+4, &file, sizeof(file));
    memread(f->esp+8, &initial_size, sizeof(initial_size));

    if(get_user((const uint8_t*)file) == -1) exit(-1);
    if(!file) exit(-1);

    lock_acquire(&file_lock);
    f->eax = filesys_create(file, initial_size);
    lock_release(&file_lock);
    
    break;
  }
  case SYS_REMOVE:
  {
    const char* file;

    memread(f->esp+4, &file, sizeof(file));
    
    if(get_user((const uint8_t*)file)==-1) exit(-1);
    
    lock_acquire(&file_lock);
    f->eax = filesys_remove(file);
    lock_release(&file_lock);
 
    break;
  }
  case SYS_OPEN:
  {
    const char* file;

    memread(f->esp + 4, &file, sizeof(file));	  
    
    if(get_user((const uint8_t*)file)==-1) exit(-1);
    if(!file) exit(-1);

    lock_acquire(&file_lock);
    struct file* openfile;
    struct file_descriptor* myfd = palloc_get_page(0);

    if(myfd){
      openfile = filesys_open(file);
      if(!openfile){
        palloc_free_page(myfd);
	f->eax = -1;
      }
      else{
        myfd->file = openfile;
       	struct list *fd_list = &thread_current()->file_descriptors;
        
        if(list_empty(fd_list)) myfd->id = 3;
	else myfd->id = (list_entry(list_back(fd_list), struct file_descriptor, elem)->id) + 1;
        list_push_back(fd_list, &(myfd->elem));

	f->eax = myfd->id;
      }
    }    
    else {
      f->eax = -1;
    }
    lock_release(&file_lock);
    
    break;
  }
  case SYS_FILESIZE:
  {
    int fd;

    memread(f->esp + 4, &fd, sizeof(fd));
    
    lock_acquire(&file_lock);
    struct file_descriptor* myfd = find_fd(fd);
    if(myfd && myfd->file){
      f->eax = file_length(myfd->file);
    }
    else {
      f->eax = -1;
    }
    lock_release(&file_lock);
    break;
  }
  case SYS_READ:
  {  
    int fd;
    void* buffer;
    unsigned size;

    memread(f->esp + 4, &fd, sizeof(fd));
    memread(f->esp + 8, &buffer, sizeof(buffer));
    memread(f->esp + 12, &size, sizeof(size));
    
    if(get_user((const uint8_t*)buffer)==-1) exit(-1);
    if(get_user((const uint8_t*)buffer+size-1)==-1) exit(-1);

    lock_acquire(&file_lock);
    if(fd == 0){
      unsigned i;
      for(i = 0; i < size; i++){
        if(!put_user(buffer + i, input_getc())){
          lock_release(&file_lock);
          exit(-1);
	}
      }
      f->eax = size;
    }
    else{
      struct file_descriptor* myfd = find_fd(fd);
      if(myfd && myfd->file){
	f->eax = file_read(myfd->file, buffer, size);
      }
      else {
	f->eax = -1;
      }
    }
    lock_release(&file_lock);
    
    break;
  }
  case SYS_WRITE:
  {  
    int fd;
    const void* buffer;
    unsigned size;

    memread(f->esp + 4, &fd, sizeof(fd));
    memread(f->esp + 8, &buffer, sizeof(buffer));
    memread(f->esp + 12, &size, sizeof(size));    
    
    if(get_user((const uint8_t*)buffer)==-1) exit(-1);
    if(get_user((const uint8_t*)buffer+size-1)==-1) exit(-1);

    lock_acquire(&file_lock);
    if(fd == 1){
      putbuf(buffer, size);
      f->eax = size;
    }
    else{
      struct file_descriptor* myfd = find_fd(fd);
      if(myfd && myfd->file){
	f->eax = file_write(myfd->file, buffer, size);
      }
      else {
	f->eax = -1;
      }
    }
    lock_release(&file_lock);
    
    break;
  }
  case SYS_SEEK:
  {
    int fd;
    unsigned position;

    memread(f->esp+4, &fd, sizeof(fd));
    memread(f->esp+8, &position, sizeof(position));
    
    lock_acquire(&file_lock);
    struct file_descriptor* myfd = find_fd(fd);
    if(myfd && myfd->file){
      file_seek(myfd->file, position);
    }
    lock_release(&file_lock);
    
    break;
  }
  case SYS_TELL:
  {
    int fd;

    memread(f->esp+4, &fd, sizeof(fd));
	   
    lock_acquire(&file_lock);
    struct file_descriptor* myfd = find_fd(fd);
    if(myfd && myfd->file){
      f->eax = file_tell(myfd->file);
    }
    else {
      f->eax = -1;
    }
    lock_release(&file_lock);
    
    break;
  }
  case SYS_CLOSE:
  {
    int fd;

    memread(f->esp+4, &fd, sizeof(fd));
    
    lock_acquire(&file_lock);
    struct file_descriptor* myfd = find_fd(fd);
    if(myfd && myfd->file){
      list_remove(&(myfd->elem));
      file_close(myfd->file);
      palloc_free_page(myfd);
    }
    lock_release(&file_lock);
    
    break;
  }
  case SYS_MMAP:
  {
    int fd;
    void *addr;
    memread(f->esp + 4, &fd, sizeof(fd));
    memread(f->esp + 8, &addr, sizeof(addr));
    
    f->eax = mmap(fd, addr);  

    break;
  }
  case SYS_MUNMAP:
  {
    int mid;
    
    memread(f->esp + 4, &mid, sizeof(mid));
    
    munmap(mid);
    
    break;
  }
  default:
    exit(-1);

    break;
  }
}

static int get_user(const uint8_t *uaddr){
  if(!is_user_vaddr((const void*)uaddr)) return -1;

  int result;
  asm("movl $1f, %0; movzbl %1, %0; 1:"
     : "=&a" (result) : "m" (*uaddr));
  return result;
}

static bool put_user (uint8_t *udst, uint8_t byte){
  if(!is_user_vaddr((const void*)udst)) return false;

  int error_code;

  asm("movl $1f, %0; movb %b2, %1; 1:"
     : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

int memread (void* src, void* dst, size_t bytes){
  int32_t value;
  size_t i;
  
  for(i = 0; i < bytes; i++){
    value = get_user(src+i);
    if(value == -1) exit(-1);
    *(char*)(dst+i) = value;
  }

  return (int)bytes;
}

struct file_descriptor* find_fd(int fd){
  struct thread *cur = thread_current();
  struct list_elem *e;
  
  if(fd < 3) return NULL;

  if(list_empty(&cur->file_descriptors)) return NULL;

  for(e = list_begin(&cur->file_descriptors); e != list_end(&cur->file_descriptors); e = list_next(e)){
    struct file_descriptor *temp = list_entry(e, struct file_descriptor, elem);
    if (temp->id == fd) return temp;
  }
  
  return NULL;
}

static struct mmap_descriptor * find_md (int mid) {
  struct thread *cur = thread_current();
  struct list_elem *e;

  if(list_empty(&cur->mmap_descriptors)) return NULL;
  for( e = list_begin(&cur->mmap_descriptors); e!= list_end(&cur->mmap_descriptors); e = list_next(e)){
    struct mmap_descriptor *temp = list_entry(e, struct mmap_descriptor, elem);
    if(temp->id == mid) return temp;
  }

  return NULL;
}

void exit(int status){
  struct thread* cur = thread_current();
  cur->exit = status;
  
  printf("%s: exit(%d)\n", cur->name, status);

  thread_exit();
}

int mmap(int fd, void *upage) {
  if( upage == NULL || pg_ofs(upage)) return -1;

  struct thread *cur = thread_current();

  lock_acquire(&file_lock);

  struct file *f = NULL;
  struct file_descriptor* f_descriptor = find_fd(fd);
  if(f_descriptor && f_descriptor->file) {
    f = file_reopen (f_descriptor->file);
  }

  if(f==NULL)
  {
    lock_release(&file_lock);
    return -1;
  }
  
  size_t file_size = file_length(f);
  if(file_size == 0)
  {
    lock_release(&file_lock);
    return -1;
  }
  
  if(vm_find_spage(cur->spt, upage)!=NULL || vm_find_spage(cur->spt, upage + file_size*PGSIZE)!=NULL){
    lock_release(&file_lock);
    return -1;
  }

  size_t i;
  for(i = 0; i < file_size; i += PGSIZE) {
    void *addr = upage + i;

    size_t read_bytes;
    if(i + PGSIZE < file_size)
      read_bytes = PGSIZE;
    else
      read_bytes = file_size - i;

    vm_spage_table_install(cur->spt, FILE_SYS, upage + i, NULL, 0, f, i, read_bytes, PGSIZE - read_bytes, true);
  }

  int mid = 1;
  if(!list_empty(&cur->mmap_descriptors))
    mid = list_entry(list_back(&cur->mmap_descriptors), struct mmap_descriptor, elem)->id + 1;

  struct mmap_descriptor *m_descriptor = (struct mmap_desc*) malloc(sizeof (struct mmap_descriptor));
  m_descriptor->id = mid;
  m_descriptor->file = f;
  m_descriptor->addr = upage;
  m_descriptor->size = file_size;
  list_push_back (&cur->mmap_descriptors, &m_descriptor->elem);

  lock_release(&file_lock);
  return mid;
}

void munmap(int mid)
{
  struct thread *cur = thread_current();
  struct mmap_descriptor *m_descriptor = find_md(mid);

  if(m_descriptor == NULL)
    return false;

  lock_acquire (&file_lock);
  size_t file_size = m_descriptor->size;
  void* addr = m_descriptor->addr;
  size_t i;
  for(i = 0; i < file_size; i += PGSIZE) {
    size_t bytes;
    if (i + PGSIZE < file_size) bytes = PGSIZE;
    else bytes = file_size - i;

    vm_spage_table_mm_unmap (cur->spt, cur->pagedir, addr + i, m_descriptor->file, i, bytes);
  }

  list_remove(&m_descriptor->elem);
  file_close(m_descriptor->file);
  free(m_descriptor);
  lock_release(&file_lock);
}
