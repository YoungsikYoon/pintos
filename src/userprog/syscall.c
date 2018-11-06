#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void is_valid_vaddr (void* x);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int *sys, *fd, *buffer, *size;

  sys = (int*)f->esp;
  fd = (int*)f->esp + 1;
  buffer = (int*)f->esp + 2;
  size = (int*)f->esp + 3;

  is_valid_buffer(sys);

  if(*sys == SYS_HALT){
    shutdown_power_off();
  }

  if(*sys == SYS_WRITE){

  }
}
 
void is_valid_vaddr (void* x){
  if(!is_user_vaddr(x)) exit(-1);
}
