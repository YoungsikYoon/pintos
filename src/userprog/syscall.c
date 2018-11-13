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

static void syscall_handler (struct intr_frame *);

static int get_user(const uint8_t *uaddr);
static bool put_user(uint8_t *udst, uint8_t byte);
int memread(void *src, void *dst, size_t bytes);
static struct file_descriptor* find_fd(int fd);

void exit(int status);
struct lock file_lock;

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

  if (sys_num == SYS_HALT) {
    shutdown_power_off();
  }

  else if (sys_num == SYS_EXIT) {
    int status;
    memread(f->esp+4,&status, sizeof(status));
    
    exit(status);
  }

  else if (sys_num == SYS_EXEC) {
    const void* cmd_line;
    memread(f->esp+4, &cmd_line, sizeof(cmd_line));
   
    f->eax = process_execute(cmd_line);
  }

  else if (sys_num == SYS_WAIT) {
    int pid;
    memread(f->esp+4, &pid, sizeof(pid));

    f->eax = process_wait(pid);
  }

  else if (sys_num == SYS_CREATE) {
    const char* file;
    unsigned initial_size;
    memread(f->esp+4, &file, sizeof(file));
    memread(f->esp+8, &initial_size, sizeof(initial_size));
    
    if(!file) exit(-1);
 
    f->eax = filesys_create(file, initial_size);
  }

  else if (sys_num == SYS_REMOVE) {
    const char* file;
    memread(f->esp+4, &file, sizeof(file));
    
    f->eax = filesys_remove(file);
  }

  else if (sys_num == SYS_OPEN) {
    const char* file;
    memread(f->esp + 4, &file, sizeof(file));	  

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
  }

  else if (sys_num == SYS_FILESIZE) {
    int fd;
    memread(f->esp + 4, &fd, sizeof(fd));

    struct file_descriptor* myfd = find_fd(fd);
    if(myfd && myfd->file){
      f->eax = file_length(myfd->file);
    }
    else {
      f->eax = -1;
    }
  }

  else if (sys_num == SYS_READ) {
    int fd;
    void* buffer;
    unsigned size;
    memread(f->esp + 4, &fd, sizeof(fd));
    memread(f->esp + 8, &buffer, sizeof(buffer));
    memread(f->esp + 12, &size, sizeof(size));
    	  
    if(!is_user_vaddr((const void*)buffer) || !is_user_vaddr((const void*)(buffer + size - 1))) exit(-1);
    
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
  }

  else if (sys_num == SYS_WRITE) {
    int fd;
    const void* buffer;
    unsigned size;
    memread(f->esp + 4, &fd, sizeof(fd));
    memread(f->esp + 8, &buffer, sizeof(buffer));
    memread(f->esp + 12, &size, sizeof(size));    
    
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
  }

  else if (sys_num == SYS_SEEK) {
    int fd;
    unsigned position;
    memread(f->esp+4, &fd, sizeof(fd));
    memread(f->esp+8, &position, sizeof(position));
    
    struct file_descriptor* myfd = find_fd(fd);
    if(myfd && myfd->file){
      file_seek(myfd->file, position);
    }
  }

  else if (sys_num == SYS_TELL) {
    int fd;
    memread(f->esp+4, &fd, sizeof(fd));
	   
    struct file_descriptor* myfd = find_fd(fd);
    if(myfd && myfd->file){
      f->eax = file_tell(myfd->file);
    }
    else {
      f->eax = -1;
    }
  }

  else if (sys_num == SYS_CLOSE) {
    int fd;
    memread(f->esp+4, &fd, sizeof(fd));

    struct file_descriptor* myfd = find_fd(fd);
    if(myfd && myfd->file){
      list_remove(&(myfd->elem));
      file_close(myfd->file);
      palloc_free_page(myfd);
    }
  }

  else exit(-1);
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

void exit(int status){
  struct thread* cur = thread_current();
  cur->exit = status;
  printf("%s: exit(%d)\n", cur->name, status);

  thread_exit();
}
