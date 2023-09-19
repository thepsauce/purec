#ifndef INCLUDED_PUREC_H
#define INCLUDED_PUREC_H

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <curses.h>

#include <magic.h>
#include <iconv.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define ARRLEN(a) (sizeof(a)/sizeof*(a))

#define MAX(a, b) ({ \
	__auto_type _MAX_a = (a); \
	__auto_type _MAX_b = (b); \
	_MAX_a > _MAX_b ? _MAX_a : _MAX_b; \
})

#define MIN(a, b) ({ \
	__auto_type _MIN_a = (a); \
	__auto_type _MIN_b = (b); \
	_MIN_a < _MIN_b ? _MIN_a : _MIN_b; \
})

char *read_file_utf8(const char *path, size_t *dest_ndata);

/* Error codes (extension to errno) */
extern int purec_errno;
enum {
	PESUCCESS,

	PEFIRST_SYSCALL,
	/* failed system calls */
	PEMALLOC,
	PESTAT,
	PEOPEN,
	PEICONV,

	PELASR_SYSCALL,

	PERACCESS, /* no read access */
	PEWACCESS, /* no write access */
	PECOUTSIDE, /* file changed outside */
	PEPATH, /* problem with the input path occured */
};

#include "buffer.h"
#include "window.h"

#endif
