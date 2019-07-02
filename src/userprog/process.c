#include "userprog/process.h"
#include "userprog/syscall.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads/synch.h>
#include <threads/malloc.h>
#include <devices/timer.h>
#include <vm/page.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "vm/frame.h"

/* The implementation of process_wait
   process_wait is used to wait for a child process to exit and get its
   exitcode. We discuss some details in our implementation here.
   - How can a process know who is its child?
     A vector of tid_t is maintained in struct thread. Each time the process
     spawn a child process, its tid is added into this vector. Note that this
     vector will never shrink, since even if the child process has exited,
     the parent process can still call process_wait on it.
   - How can a process get the exitcode of the child process?
     A hash table is maintained by the kernel, which maps the tid to the
     corresponding exitcode. For the details, see the next entry.
   - How is the hash table maintained?
     In short, when a process is created, it is added into the hash table;
     when it exits, it update its exitcode which is contained in the entry
     and remove its children's entries.
         Note that the purpose of this hash table is to provide a way for
     parent process to determine the status of a child process. This is the
     reason why we remove children's entries, instead of the process's.
         To avoid the invalidation of pointers which may occur when we expand
     the hash table, the table store the pointers to the entries, instead of
     the entries themselves.
   - How process_wait is implemented?
     First, we shall determine whether the provided tid is valid. After that,
     we acquire the lock of the hash table and find the entry (pointer) we
     want. Now we can release the lock since the change of the structure of
     the hash table will never affect the content of the entry. Then, we
     acquire the lock of that entry, check the exitcode and return or wait
     on the conditional variable depending on the result.
   */

#define STATUS_RUNNING -256
#define STATUS_ERROR -257

/* The hash table from tids to status */
struct hash_entry
  {
    tid_t tid;
    int exitcode;
    struct condition cv;
    struct lock lk;
  };

struct lock hash_table_lock;
struct condition hash_table_cv;
size_t hash_table_capacity;
size_t hash_table_size;
static struct hash_entry **hash_table;

static void
init_hash_table ()
{
  hash_table_size = 0;
  hash_table_capacity = 8;
  hash_table = malloc (sizeof (struct hash_entry*) * hash_table_capacity);
  ASSERT (hash_table)
  for (int i = 0; i < hash_table_capacity; ++i)
    hash_table[i] = NULL;
  lock_init (&hash_table_lock);
  cond_init (&hash_table_cv);
}

static struct hash_entry*
tid_to_hash_entry (tid_t tid)
{
  ASSERT (tid >= 0)

  int i = tid % hash_table_capacity;
  while (hash_table[i] != NULL && hash_table[i]->tid != tid)
    {
      i = hash_table_capacity - 1 ? 0 : i + 1;
    }
  return hash_table[i];
}

static struct hash_entry*
make_hash_entry(tid_t tid)
{
  struct hash_entry* res = malloc (sizeof (struct hash_entry));
  ASSERT (res)
  res->tid = tid;
  res->exitcode = STATUS_RUNNING;
  cond_init (&res->cv);
  lock_init (&res->lk);
  return res;
}

static bool
insert_hash_entry (tid_t tid, struct hash_entry *entry)
{
  ASSERT (tid_to_hash_entry (tid) == NULL)
  if (hash_table_size > hash_table_capacity / 2)
    {
      struct hash_entry** old_table = hash_table;
      hash_table =
          malloc (sizeof (struct hash_entry *) * hash_table_capacity * 2);
      ASSERT (hash_table)
      for (int i = 0; i < hash_table_capacity * 2; ++i)
        hash_table[i] = NULL;

      hash_table_capacity *= 2;
      hash_table_size = 0;

      for (int i = 0; i < hash_table_capacity / 2; ++i)
        {
          if (old_table[i] == NULL)
            continue;
          bool res = insert_hash_entry (old_table[i]->tid, old_table[i]);
          ASSERT (res)
        }
      free (old_table);
    }

  int i = tid % hash_table_capacity;
  while (hash_table[i] != NULL)
    i = hash_table_capacity - 1 ? 0 : i + 1;
  hash_table[i] = entry;
  ++hash_table_size;
  return true;
}

static void
erase_tid (tid_t tid)
{
  int i = tid % hash_table_capacity;
  while (hash_table[i] != NULL && hash_table[i]->tid != tid)
    i = hash_table_capacity - 1 ? 0 : i + 1;
  ASSERT (hash_table[i]->tid == tid)
  free (hash_table[i]);
  hash_table[i] = NULL;
}

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

void
process_init ()
{
  init_hash_table ();
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *cmd)
{
  // printf ("[DEBUG] process_execute %s\n", cmd);
  char *cmd_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  cmd_copy = palloc_get_page (0);
  if (cmd_copy == NULL)
    return TID_ERROR;
  strlcpy (cmd_copy, cmd, PGSIZE);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (cmd, PRI_DEFAULT, start_process, cmd_copy);

  lock_acquire (&hash_table_lock);
  while (tid_to_hash_entry (tid) == NULL)
    cond_wait (&hash_table_cv, &hash_table_lock);
  int status = tid_to_hash_entry (tid)->exitcode;
  lock_release (&hash_table_lock);

  if (status == STATUS_ERROR)
    return TID_ERROR;

  if (tid == TID_ERROR)
      palloc_free_page (cmd_copy);

  struct thread *pnt = thread_current ();
  if (pnt->children_array_capacity == 0)  /* main */
    {
      pnt->children_array_capacity = 4;
      pnt->children_processes =
          malloc (pnt->children_array_capacity * sizeof (tid_t));
      ASSERT (pnt->children_processes)
    }
  ASSERT (pnt->children_number <= pnt->children_array_capacity)
  if (pnt->children_array_capacity == pnt->children_number)
    {
      tid_t *old = pnt->children_processes;
      pnt->children_processes =
          malloc (sizeof (tid_t) * pnt->children_array_capacity * 2);
      ASSERT (pnt->children_processes)
      for (int i = 0; i < pnt->children_array_capacity; ++i)
        pnt->children_processes[i] = old[i];
      pnt->children_array_capacity *= 2;
      free (old);
    }
  pnt->children_processes[pnt->children_number++] = tid;

  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  /* If load failed, quit. */
  palloc_free_page (file_name);
  struct thread * cur = thread_current ();
  struct hash_entry * entry = make_hash_entry (cur->tid);
  if (!success)
    {
      cur->exitcode = STATUS_ERROR;
      entry->exitcode = STATUS_ERROR;
    }
  lock_acquire (&hash_table_lock);
  bool insert_success = insert_hash_entry (cur->tid, entry);
  ASSERT (insert_success)

	printf("see all threads\n");
  thread_print_all();
  printf("see threads waiting for the condition variable\n");
  if (!list_empty(&hash_table_cv.waiters)) {
	  for (struct list_elem *e = list_begin(&hash_table_cv.waiters); e != list_end(&hash_table_cv.waiters); e = e->next) {
		  struct semaphore_elem *waiter_semaphore = list_entry(e, struct semaphore_elem, elem);
		  thread_print_one(waiter_semaphore->wait_thread, NULL);
	  }
  }
	cond_broadcast (&hash_table_cv, &hash_table_lock);
  lock_release (&hash_table_lock);

  if (!success)
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{
  /* Check the validation of child_tid */
  struct thread * cur = thread_current ();
  bool found = false;
  for (int i = 0; i < cur->children_number; ++i)
    {
      if (cur->children_processes[i] == child_tid)
        {
          found = true;
          break;
        }
    }
  if (!found)
    return -1;

  /* Wait */
  lock_acquire (&hash_table_lock);
  struct hash_entry *entry = tid_to_hash_entry (child_tid);
  ASSERT (entry)
  lock_release (&hash_table_lock);

  lock_acquire (&entry->lk);
  while (entry->exitcode == STATUS_RUNNING)
    {
      ASSERT (intr_get_level () == INTR_ON)
      cond_wait (&entry->cv, &entry->lk);
    }
  int exitcode = entry->exitcode;
  entry->exitcode = -1;
  lock_release (&entry->lk);

  return exitcode;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  if (strlen (cur->exe_name) && cur->exitcode != STATUS_ERROR)
    printf ("%s: exit(%d)\n", cur->exe_name, cur->exitcode);

  if (is_holding_fs_lock())
    {
      release_fs_lock();
    }

  if (lock_held_by_current_thread(&frame_lock))
    {
      release_frame_lock();
    }

  /* Close opened files and free file_descriptors. */
  struct list * fd_list = &cur->file_descriptors;
  while (!list_empty(fd_list))
    {
      struct list_elem * e = list_front (fd_list);
      struct file_descriptor *fd = list_entry(e, struct file_descriptor, elem);
      list_pop_front(fd_list);
      fs_close(fd->file);
      free(fd);
    }

  struct list *mmap_list = &cur->mmap_lsit;
  while (!list_empty(mmap_list))
    {
      struct list_elem *e = list_front(mmap_list);
      struct mmap_info *mmap_info = list_entry(e, struct mmap_info, elem);
      sys_munmap(mmap_info->id);
    }

  /* Update the hash table */
  if (lock_held_by_current_thread (&hash_table_lock))
    {
      // printf ("[DEBUG] %s, pid = %d\n", cur->exe_name, cur->tid);
      thread_current ();
      debug_backtrace_all ();
      ASSERT (false)
    }
  lock_acquire (&hash_table_lock);
  struct hash_entry *entry = tid_to_hash_entry (cur->tid);
  for (int i = 0; i < cur->children_number; ++i)
    erase_tid (cur->children_processes[i]);
  lock_release (&hash_table_lock);

  /* Note that if the parent process has exited, the entry of this
     process will have been removed. */
  if (entry)
    {
      fs_close (cur->executable_file);
      lock_acquire (&entry->lk);
      ASSERT (entry->exitcode == STATUS_RUNNING ||
              entry->exitcode == STATUS_ERROR)
      entry->exitcode = cur->exitcode;
      cond_broadcast (&entry->cv, &entry->lk);
      lock_release (&entry->lk);
    }

  acquire_frame_lock();
  supp_page_table_destroy(&cur->supp_page_table);
  release_frame_lock();

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  // printf ("[DEBUG] Loading %s... \n", file_name);

  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

//  printf("[DEBUG]%d supp_page_table_init\n", thread_current()->tid);
  supp_page_table_init(&t->supp_page_table);

  /* Open executable file. */
  char exe_name[128] = { 0 };
  for (i = 0; file_name[i] != ' ' && file_name[i] != 0; ++i)
    {
      exe_name[i] = file_name[i];
    }
  exe_name[i] = 0;
  strlcpy (t->exe_name, exe_name, 64);

  file = fs_open (exe_name);
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", exe_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (fs_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > fs_length (file))
        goto done;
      fs_seek (file, file_ofs);

      if (fs_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;
  ASSERT (*esp == PHYS_BASE)

  /* Parse the command line arguments */
  {
    const int MAX_ARGC = 128;  // TODO
    const int MAX_FILE_NAME_LEN = 128;
    int file_name_len = strlen (file_name);
    ASSERT (file_name_len <= MAX_FILE_NAME_LEN)
    *esp = (char *) (*esp) - file_name_len - 1;
    strlcpy (*esp, file_name, MAX_FILE_NAME_LEN);

    int argc = 0;
    char *argv[MAX_ARGC];
    {
      char *save_ptr;
      for (char *tok = strtok_r (*esp, " ", &save_ptr); tok != NULL;
           tok = strtok_r (NULL, " ", &save_ptr))
        {
          argv[argc++] = tok;
          ASSERT (argc <= MAX_ARGC)
        }
    }

    *esp = (char *)(*esp) - ((uintptr_t)(*esp) % 4);  /* alignment */

    *esp = (char *)(*esp) - 4;
    *(int *)(*esp) = 0;

    *esp = (char *)(*esp) - argc * 4;
    memcpy (*esp, argv, argc * 4);

    *esp = (char *)(*esp) - 4;
    *(void **)(*esp) = (char *)(*esp) + 4;

    *esp = (char *)(*esp) - 4;
    *(int *)(*esp) = argc;

    *esp = (char *)(*esp) - 4;  /* The fake return address */
    *(int *)(*esp) = 0;
  }

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  if (success)
    fs_deny_write (file);
  thread_current ()->executable_file = file;

  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) fs_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  fs_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */


      acquire_frame_lock();
      uint8_t *kpage = allocate_frame (PAL_USER, upage);
      release_frame_lock();
      if (kpage == NULL)
        return false;


      /* Load this page. */
      off_t length = fs_read (file, kpage, page_read_bytes);
      ASSERT (length == page_read_bytes)

      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      bool success = install_page (upage, kpage, writable);
      ASSERT (success);

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;

    }

  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  acquire_frame_lock();
  kpage = allocate_frame (PAL_USER | PAL_ZERO, ((uint8_t *) PHYS_BASE) - PGSIZE);
  release_frame_lock();
  if (kpage == NULL)
    return false;

  success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
  if (success)
    *esp = PHYS_BASE;
  else
    {
      acquire_frame_lock();
      free_frame(kpage, true);
      release_frame_lock();
    }

  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  if (pagedir_get_page(t->pagedir, upage) != NULL)
    return false;
  if (!pagedir_set_page (t->pagedir, upage, kpage, writable))
    return false;

  return set_supp_frame_entry(&t->supp_page_table, upage, kpage, writable);

}
