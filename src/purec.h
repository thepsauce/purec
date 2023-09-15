#ifndef INCLUDED_PUREC_H
#define INCLUDED_PUREC_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <magic.h>
#include <iconv.h>

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

#include "buffer.h"

#endif
