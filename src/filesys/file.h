#ifndef FILESYS_FILE_H
#define FILESYS_FILE_H

#include "filesys/off_t.h"

struct inode;

/* Opening and closing files. */
struct file *file_open (struct inode *);
struct file *file_reopen (struct file *);
void file_close (struct file *);
struct inode *file_get_inode (struct file *);

/* Reading and writing. */
off_t file_read (struct file *, void *, off_t);
off_t file_read_at (struct file *, void *, off_t size, off_t start);
off_t file_write (struct file *, const void *, off_t);
off_t file_write_at (struct file *, const void *, off_t size, off_t start);

/* Preventing writes. */
void file_deny_write (struct file *);
void file_allow_write (struct file *);

/* File position. */
void file_seek (struct file *, off_t);
off_t file_tell (struct file *);
off_t file_length (struct file *);

struct file *fs_reopen (struct file *file);
void fs_close (struct file *file);
off_t fs_read (struct file *file, void *buffer, off_t size);
off_t fs_read_at (struct file *file, void *buffer, off_t size, off_t file_ofs);
off_t fs_write (struct file *file, const void *buffer, off_t size);
off_t fs_write_at (struct file *file, const void *buffer, off_t size, off_t file_ofs);
void fs_deny_write (struct file *file);
void fs_seek (struct file *file, off_t new_pos);
off_t fs_tell (struct file *file);
off_t fs_length (struct file *file);

#endif /* filesys/file.h */
