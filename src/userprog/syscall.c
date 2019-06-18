#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <threads/synch.h>
#include <filesys/filesys.h>
#include <threads/malloc.h>
#include <filesys/file.h>
#include <devices/input.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "pagedir.h"

#include "devices/shutdown.h"
#include "process.h"
#include "user/syscall.h"


/* Ensure only one thread at a time is accessing file system. */
struct lock fs_lock;

static void syscall_handler (struct intr_frame *);



void
syscall_init (void) 
{
  lock_init(&fs_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static bool sys_create (const char *file, unsigned initial_size);

static bool sys_remove (const char *file);

static int sys_open (const char *file);

static int sys_filesize (int fd_id);

static int sys_read(int fd_id, void *buffer, unsigned length);

static int sys_write (int fd_id, const void *buffer, unsigned length);

static void sys_seek (int fd_id, unsigned position);

static unsigned sys_tell (int fd_id);

static void sys_close (int fd_id);

static void sys_exit (int status);

static pid_t sys_exec (const char *cmd_line) {

}

/* TODO: this check may be wrong */
static void check_legal (const void *uaddr);

static int get_user (const uint8_t *uaddr);

static bool put_user (uint8_t *udst, uint8_t byte);

static struct file_descriptor* get_file_descriptor(struct thread * t, int fd_id);

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  char * esp = f->esp;
  int sys_num = *(int*)esp;
  /*printf("[DEBUG]sys_num = %d\n ", sys_num);*/
  switch (sys_num)
    {
      case SYS_HALT:
        {
          shutdown_power_off();
          NOT_REACHED ();
        }
      case SYS_EXIT:
        {
          // printf ("[DEBUG] SYS_EXIT\n");
          sys_exit(*(int *) (esp + 4));
          break;
        }
      case SYS_CREATE:
        {
          const char * file = *(char **)(esp + 4);
          check_legal(file);
          unsigned initial_size = *(unsigned *)(esp + 8);
          f->eax = (uint32_t) sys_create(file, initial_size);
          break;
        }
      case SYS_REMOVE:
        {
          const char * file = *(char **)(esp + 4);
          check_legal(file);
          f->eax = (uint32_t) sys_remove(file);
          break;
        }
      case SYS_OPEN:
        {
          const char * file = *(char **)(esp + 4);
          check_legal(file);
          f->eax = (uint32_t) sys_open(file);
          break;
        }
      case SYS_FILESIZE:
        {
          int fd_id = *(int *)(esp + 4);
          f->eax = (uint32_t) sys_filesize(fd_id);
          break;
        }
      case SYS_READ:
        {
          int fd_id = *(int *)(esp + 4);
          void * buffer = *(void **)(esp + 8);
          check_legal(buffer);
          unsigned length = *(unsigned *)(esp + 12);
          check_legal((uint8_t *) buffer + length - 1);
          f->eax = (uint32_t) sys_read(fd_id, buffer, length);
          break;
        }
      case SYS_WRITE:
        {
          // printf ("[DEBUG] SYS_WRITE\n");
          int fd_id = *(int *)(esp + 4);
          const void *buffer = *(void **)(esp + 8);
          check_legal(buffer);
          unsigned length = *(unsigned *)(esp + 12);
          check_legal((const uint8_t *) buffer + length - 1);
          f->eax = (uint32_t) sys_write (fd_id, buffer, length);
          break;
        }
      case SYS_SEEK:
        {
          int fd_id = *(int *)(esp + 4);
          unsigned position = *(unsigned *)(esp + 8);
          sys_seek(fd_id, position);
          break;
        }
      case SYS_TELL:
        {
          int fd_id = *(int *)(esp + 4);
          f->eax = (uint32_t) sys_tell(fd_id);
          break;
        }
      case SYS_CLOSE:
        {
          int fd_id = *(int *)(esp + 4);
          sys_close(fd_id);
          break;
        }
      default:
        ASSERT (false)
    }
  /* thread_exit (); */
}

static void sys_exit (int status)
{
  // printf ("[DEBUG] sys_exit with status = %d\n", status);
  // TODO
  printf ("%s: exit(%d)\n", thread_current ()->name, status);

  thread_exit ();
}


bool
sys_create (const char *file, unsigned initial_size)
{
  bool success;
  lock_acquire(&fs_lock);
  success = filesys_create(file, initial_size);
  lock_release(&fs_lock);
  return success;
}

bool
sys_remove (const char *file)
{
  bool success;
  lock_acquire(&fs_lock);
  success = filesys_remove(file);
  lock_release(&fs_lock);
  return success;
}

int
sys_open (const char *file)
{
  struct file * f;
  struct file_descriptor * fd = malloc(sizeof(struct file_descriptor));
  if (!fd)
    return -1;

  lock_acquire(&fs_lock);
  f = filesys_open(file);
  lock_release(&fs_lock);
  if (!f)
    {
      free(fd);
      return -1;
    }
  fd->file = f;
  struct list * fd_list = &thread_current()->file_descriptors;
  if (list_empty(fd_list))
    fd->id = 2;
  else
    fd->id = (list_entry(list_back(fd_list), struct file_descriptor, elem)->id) + 1;
  list_push_back(fd_list, &fd->elem);
  return fd->id;
}

int
sys_filesize (int fd_id)
{
  int size;
  struct file_descriptor * fd = get_file_descriptor(thread_current(), fd_id);
  if (!fd)
    return -1;
  lock_acquire(&fs_lock);
  size = file_length(fd->file);
  lock_release(&fs_lock);
  return size;
}

int
sys_read(int fd_id, void *buffer, unsigned length)
{
  if (fd_id == STDIN_FILENO)
    {
      uint8_t * udst = (uint8_t *) buffer;
      for (unsigned i = 0; i < length; ++i)
        {
          if (!put_user(udst, input_getc())) {
            sys_exit(-1);
          }
          ++udst;
        }
      return length;
    }

  if (fd_id == STDOUT_FILENO)
    return -1;

  int size;
  struct file_descriptor * fd = get_file_descriptor(thread_current(), fd_id);
  if (!fd)
    return -1;
  lock_acquire(&fs_lock);
  size = file_read(fd->file, buffer, length);
  lock_release(&fs_lock);
  return size;
}

int
sys_write (int fd_id, const void *buffer, unsigned length)
{
  if (fd_id == STDIN_FILENO) {
    return -1;
  }

  if (fd_id == 1)
    {
      putbuf ((char *)buffer, length);
      return length;
    }

  /* printf ("fd = %d\n", fd_id); */
  /* ASSERT (false && "fd should be 1") */

  int size;
  struct file_descriptor * fd = get_file_descriptor(thread_current(), fd_id);
  if (!fd)
    return -1;
  lock_acquire(&fs_lock);
  size = file_write(fd->file, buffer, length);
  lock_release(&fs_lock);
  return size;
}

void
sys_seek (int fd_id, unsigned position)
{
  struct file_descriptor * fd = get_file_descriptor(thread_current(), fd_id);
  if (!fd)
    return;
  lock_acquire(&fs_lock);
  file_seek(fd->file, (off_t) position);
  lock_release(&fs_lock);
}

unsigned
sys_tell (int fd_id)
{
  unsigned position;
  struct file_descriptor * fd = get_file_descriptor(thread_current(), fd_id);
  if (!fd)
    return 0; /* TODO: here is improper */
  lock_acquire(&fs_lock);
  position = (unsigned) file_tell(fd->file);
  lock_release(&fs_lock);
  return position;
}

void
sys_close (int fd_id)
{
  struct file_descriptor * fd = get_file_descriptor(thread_current(), fd_id);
  if (!fd)
    return;
  lock_acquire(&fs_lock);
  file_close(fd->file);
  lock_release(&fs_lock);
  list_remove(&fd->elem);
  free(fd);
}


/*************************** Auxiliary Functions *****************************/

/* TODO: this check may be wrong */
void
check_legal (const void *uaddr)
{
  struct thread *cur = thread_current();
  if (uaddr == NULL || !is_user_vaddr(uaddr) ||
      (pagedir_get_page (cur->pagedir, uaddr)) == NULL)
    sys_exit(-1);
}

int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
  : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
  : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

struct file_descriptor*
get_file_descriptor(struct thread * t, int fd_id)
{
  ASSERT(t != NULL);

  /* 0 is stdin, 1 is stdout */
  if (fd_id < 2)
    return NULL;

  struct list * fd_list = &t->file_descriptors;
  struct list_elem * e;
  for (e = list_begin(fd_list); e != list_end(fd_list); e = list_next(e))
    {
      struct file_descriptor * fd = list_entry(e, struct file_descriptor, elem);
      if (fd->id == fd_id)
        return fd;
    }
  return NULL;
}