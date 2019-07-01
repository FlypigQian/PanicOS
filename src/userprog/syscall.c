#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <threads/synch.h>
#include <filesys/filesys.h>
#include <threads/malloc.h>
#include <filesys/file.h>
#include <devices/input.h>
#include <vm/page.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "pagedir.h"

#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "process.h"
#include "threads/synch.h"
#include "user/syscall.h"
#include "vm/frame.h"


static void syscall_handler (struct intr_frame *);

/* TODO: this check may be wrong */
static void check_legal (const void *uaddr);

static void check_valid(const void *uaddr);

void
syscall_init (void) 
{
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

static mapid_t sys_mmap (int fd_id, void *start_addr);

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
      case SYS_MUNMAP:
        argc = 2;
        break;
      case SYS_CREATE:
      case SYS_SEEK:
      case SYS_MMAP:
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

static struct file_descriptor* get_file_descriptor(struct thread *t, int fd_id);

static struct mmap_info* get_mmap_info(struct thread *t, int mapid);

static void load_and_pin_buffer(const void *buffer, unsigned length);

static void unpin_buffer(const void *buffer, unsigned length);

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  char * esp = f->esp;

  thread_current()->user_esp = f->esp;

  int32_t args[4];
  int argc = read_sys_call_args (esp, args);
  ASSERT (argc <= 4)

  int sys_num = args[0];
//   printf ("[DEBUG]%d sys_num = %d\n", thread_current()->tid, sys_num);

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
      case SYS_MMAP:
        f->eax = sys_mmap(args[1], (void *)args[2]);
        return;
      case SYS_MUNMAP:
        sys_munmap(args[1]);
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
  success = fs_create(file, initial_size);
  return success;
}

bool
sys_remove (const char *file)
{
  check_legal (file);
  bool success;
  success = fs_remove(file);
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

  f = fs_open(file);
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
  size = fs_length(fd->file);
  return size;
}

int
sys_read(int fd_id, void *buffer, unsigned length)
{
  check_valid(buffer);
  check_valid((uint8_t *) buffer + length - 1);

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
  load_and_pin_buffer(buffer, length);
  size = fs_read(fd->file, buffer, length);
  unpin_buffer(buffer, length);
  return size;
}

int
sys_write (int fd_id, const void *buffer, unsigned length)
{
  check_legal(buffer);
  check_legal((uint8_t *) buffer + length - 1);

  if (fd_id == STDIN_FILENO)
    {
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
    {
      return -1;
    }
  load_and_pin_buffer(buffer, length);
  size = fs_write(fd->file, buffer, length);
  unpin_buffer(buffer, length);
  return size;
}

void
sys_seek (int fd_id, unsigned position)
{
  struct file_descriptor * fd = get_file_descriptor(thread_current(), fd_id);
  if (!fd)
    return;
  fs_seek(fd->file, (off_t) position);
}

unsigned
sys_tell (int fd_id)
{
  unsigned position;
  struct file_descriptor * fd = get_file_descriptor(thread_current(), fd_id);
  if (!fd)
    return 0; /* TODO: here is improper */
  position = (unsigned) fs_tell(fd->file);
  return position;
}

void
sys_close (int fd_id)
{
  struct file_descriptor * fd = get_file_descriptor(thread_current(), fd_id);
  if (!fd)
    return;
  fs_close(fd->file);
  list_remove(&fd->elem);
  free(fd);
}


static mapid_t
sys_mmap (int fd_id, void *start_addr)
{
  if (start_addr == NULL || pg_ofs(start_addr) != 0 || fd_id <= 1)
    return -1;

  struct thread *cur = thread_current();

  struct file_descriptor * fd = get_file_descriptor(cur, fd_id);
  struct file * file = NULL;
  if (fd && fd->file)
    {
      file = fs_reopen(fd->file);
    }
  if (!file)
    {
      return -1;
    }

  uint32_t length = (uint32_t) fs_length(file);

  if (length == 0)
      return -1;

  for (uint32_t offset = 0; offset < length; offset += PGSIZE)
    {
      void *addr = start_addr + offset;
      if (get_supp_entry(&cur->supp_page_table, addr) != NULL)
          return -1;
    }

  for (uint32_t offset = 0; offset < length; offset += PGSIZE)
    {
      void *addr = start_addr + offset;
      uint32_t read_bytes = offset + PGSIZE <= length ? PGSIZE : length - offset;
      if (!set_supp_mmap_entry(&cur->supp_page_table, addr, file, offset, read_bytes))
        PANIC ("set_supp_mmap_entry failed");
    }

  struct mmap_info *mmap_info = malloc(sizeof(struct mmap_info));

  mapid_t mapid;
  if (list_empty(&cur->mmap_lsit))
    mapid = 1;
  else
    mapid = (list_entry(list_back(&cur->mmap_lsit), struct mmap_info, elem)->id) + 1;

  mmap_info->id = mapid;
  mmap_info->file = file;
  mmap_info->start_addr = start_addr;
  mmap_info->length = length;

  list_push_back(&cur->mmap_lsit, &mmap_info->elem);

  return mapid;
}

void
sys_munmap (mapid_t mapid)
{
  struct thread *cur = thread_current();
  struct mmap_info *mmap_info = get_mmap_info(cur, mapid);
  if (mmap_info == NULL)
    PANIC ("Can't find mmap_info");

  for (uint32_t offset = 0; offset < mmap_info->length; offset += PGSIZE)
    {
      void *addr = mmap_info->start_addr + offset;
      unset_supp_mmap_entry(&cur->supp_page_table, addr);
    }

  fs_close(mmap_info->file);

  list_remove(&mmap_info->elem);
  free(mmap_info);
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

void check_valid(const void *uaddr)
{
  if (uaddr == NULL || !is_user_vaddr(uaddr))
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
get_file_descriptor(struct thread *t, int fd_id)
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

struct mmap_info*
get_mmap_info(struct thread *t, int mapid)
{
  ASSERT (t != NULL);

  struct list * mmap_list = &t->mmap_lsit;
  struct list_elem * e;
  for (e = list_begin(mmap_list); e != list_end(mmap_list); e = list_next(e))
    {
      struct mmap_info * mmap_info = list_entry(e, struct mmap_info, elem);
      if (mmap_info->id == mapid)
        return mmap_info;
    }
  return NULL;
}

static void
load_and_pin_buffer(const void *buffer, unsigned length)
{
  struct thread *cur = thread_current();

  const void *buffer_end = buffer + length;
  for (void *upage = pg_round_down(buffer); upage < buffer_end; upage += PGSIZE)
  {
    if (get_user(upage) == -1)
    {
      sys_exit(-1);
    }
    struct supp_entry *entry = get_supp_entry(&cur->supp_page_table, upage);
    ASSERT (entry != NULL);
    acquire_frame_lock();
    if (!load_page(entry))
    {
      PANIC ("Loading buffer failed");
    }
    ASSERT (entry->kpage != NULL);
    pin_frame(entry->kpage);
    release_frame_lock();
  }
}

static void
unpin_buffer(const void *buffer, unsigned length)
{
  struct thread *cur = thread_current();

  const void *buffer_end = buffer + length;
  for (void *upage = pg_round_down(buffer); upage < buffer_end; upage += PGSIZE)
  {
    struct supp_entry *entry = get_supp_entry(&cur->supp_page_table, upage);
    acquire_frame_lock();
    ASSERT (entry->state == ON_FRAME);
    ASSERT (entry->kpage != NULL);
    unpin_frame(entry->kpage);
    release_frame_lock();
  }
}

