#ifndef _MAILBOX_H__
#define _MAILBOX_H__

#include <stdint.h>
#include <stdbool.h>
#include "sys/stat.h"

int open(const char *path, int flags, ...);
ssize_t read(int, void *, size_t);
ssize_t write(int, const void *, size_t);
int close(int);
_off_t lseek(int fd, _off_t offset, int whence);
int stat(const char *path, struct stat *st);

int mailbox_init(void);

#endif
