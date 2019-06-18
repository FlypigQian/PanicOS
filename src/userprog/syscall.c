#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "pagedir.h"

#include "devices/shutdown.h"
#include "process.h"
#include "user/syscall.h"

struct file_descriptor {
  int id;
  struct file* file;
  struct list_elem elem;
};

static void syscall_handler (struct intr_frame *);

bool is_legal_uaddr (const void *uaddr) {
  struct thread *cur = thread_current();
  return (uaddr != NULL && is_user_vaddr(uaddr) &&
          (pagedir_get_page (cur->pagedir, uaddr)) != NULL);
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static int sys_write (int fd, const void *buffer, unsigned size);

static void sys_exit (int status);

static pid_t sys_exec (const char *cmd_line) {

}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  char * esp = f->esp;
  int sys_num = *(int*)esp;
  esp += 4;
  switch (sys_num)
    {
//      case SYS_HALT:
//        shutdown_power_off ();
//        NOT_REACHED ()
      case SYS_EXIT:
        // printf ("[DEBUG] SYS_EXIT\n");
        sys_exit (*(int *)esp);
        return;
      case SYS_WRITE:
        {
          // printf ("[DEBUG] SYS_WRITE\n");
          int fd = *(int *)(esp);
          const void *buffer = *(void **)(esp + 4);
          unsigned size = *(int *)(esp + 8);
          sys_write (fd, buffer, size);
          return;
        }
      default:
        ASSERT (false)
    }
  thread_exit ();
}

static void sys_exit (int status) {
  // printf ("[DEBUG] sys_exit with status = %d\n", status);
  // TODO
  printf ("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();
}

static int sys_write (int fd, const void *buffer, unsigned size)
{
  if (fd == 1)
    {
      putbuf ((char *)buffer, size);
      return size;
    }
  printf ("fd = %d\n", fd);
  ASSERT (false && "fd should be 1")
}