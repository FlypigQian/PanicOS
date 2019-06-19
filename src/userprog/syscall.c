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
#include "filesys/filesys.h"
#include "process.h"
#include "threads/synch.h"
#include "user/syscall.h"

/* Ensure only one thread at a time is accessing file system. */
static struct lock fs_lock;

static void syscall_handler (struct intr_frame *);

/* TODO: this check may be wrong */
static void check_legal (const void *uaddr);

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

static int sys_read (int fd_id, void *buffer, unsigned length);

static int sys_write (int fd_id, const void *buffer, unsigned length);

static void sys_seek (int fd_id, unsigned position);

static unsigned sys_tell (int fd_id);

static void sys_close (int fd_id);

static void
sys_exit (int status) {
  thread_current ()->exitcode = status;
  thread_exit ();
}

static int
sys_wait (pid_t pid)
{
  return process_wait (pid);
}

static pid_t
sys_exec (const char *cmd_line)
{
  check_legal (cmd_line);
  check_legal (cmd_line + 1);
  check_legal (cmd_line + 2);
  check_legal (cmd_line + 3);
  pid_t pid = process_execute (cmd_line);
  if (pid == TID_ERROR)
    return -1;
  return pid;
}


/* Retrieve the system call number and arguments from the stack. The
   number and arguments will be put in ARGS, and the size of ARGS
   will be returned. */
static int
read_sys_call_args(const char *esp, int32_t args[]) {
  int argc;
  check_legal (esp);
  check_legal (esp + 1);
  check_legal (esp + 2);
  check_legal (esp + 3);

  int sys_num = *(int*)esp;
  switch (sys_num)
    {
      case SYS_HALT:
        argc = 1;
        break;
      case SYS_EXIT:
      case SYS_EXEC:
      case SYS_WAIT:
      case SYS_REMOVE:
      case SYS_OPEN:
      case SYS_FILESIZE:
      case SYS_TELL:
      case SYS_CLOSE:
        argc = 2;
        break;
      case SYS_CREATE:
      case SYS_SEEK:
        argc = 3;
        break;
      case SYS_READ:
      case SYS_WRITE:
        argc = 4;
        break;
      default:
        ASSERT (false && "Unknown system call")
    }


  for (int i = 0; i < argc; ++i)
    {
      const int32_t *addr = (const int32_t *)(esp + 4 * i);
      check_legal (addr);
      check_legal (addr + 1);
      check_legal (addr + 2);
      check_legal (addr + 3);
      args[i] = *addr;
    }
  return argc;
}


static int get_user (const uint8_t *uaddr);

static bool put_user (uint8_t *udst, uint8_t byte);

static struct file_descriptor* get_file_descriptor(struct thread * t, int fd_id);

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  char * esp = f->esp;

  int32_t args[4];
  int argc = read_sys_call_args (esp, args);
  ASSERT (argc <= 4)

  int sys_num = args[0];
  // printf ("[DEBUG] sys_num = %d\n", sys_num);

  switch (sys_num)
    {
      case SYS_HALT:
        shutdown_power_off ();
        NOT_REACHED ()
      case SYS_EXIT:
        sys_exit (args[1]);
        return;
      case SYS_EXEC:
        f->eax = sys_exec ((const char *)args[1]);
        return;
      case SYS_WAIT:
        f->eax = sys_wait (args[1]);
        return;
      case SYS_CREATE:
        f->eax = sys_create ((const char *)args[1], args[2]);
        return;
      case SYS_REMOVE:
        f->eax = sys_remove ((const char *)args[1]);
        return;
      case SYS_OPEN:
        f->eax = sys_open ((const char *)args[1]);
        return;
      case SYS_FILESIZE:
        f->eax = sys_filesize (args[1]);
        return;
      case SYS_READ:
        f->eax = sys_read (args[1], (void *)args[2], args[3]);
        return;
      case SYS_WRITE:
        f->eax = sys_write (args[1], (void *)args[2], args[3]);
        return;
      case SYS_SEEK:
        sys_seek (args[1], args[2]);
        return;
      case SYS_TELL:
        f->eax = sys_tell (args[1]);
        return;
      case SYS_CLOSE:
        sys_close (args[1]);
        return;
      default:
        ASSERT (false)
    }
  /* thread_exit (); */
}


bool
sys_create (const char *file, unsigned initial_size)
{
  check_legal (file);
  bool success;
  lock_acquire(&fs_lock);
  success = filesys_create(file, initial_size);
  lock_release(&fs_lock);
  return success;
}

bool
sys_remove (const char *file)
{
  check_legal (file);
  bool success;
  lock_acquire(&fs_lock);
  success = filesys_remove(file);
  lock_release(&fs_lock);
  return success;
}

int
sys_open (const char *file)
{
  check_legal (file);

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
  check_legal(buffer);
  check_legal((uint8_t *) buffer + length - 1);

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
  check_legal(buffer);
  check_legal((uint8_t *) buffer + length - 1);

  if (fd_id == STDIN_FILENO) {
    return -1;
  }

  if (fd_id == 1)
    {
      putbuf ((char *)buffer, length);
      return length;
    }

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
