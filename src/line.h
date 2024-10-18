#ifndef LINE_H
#define LINE_H

#include "util.h"

#include <stdlib.h>

struct raw_line {
    /// data of the line in utf8 format
    char *s;
    /// number of bytes on this line
    col_t n;
};

/**
 * Initialize a line by setting its data to given string.
 *
 * @param l     Line to initialize.
 * @param str   String to set the line to, can be `NULL`.
 * @param nstr  Length of given string.
 */
#define init_raw_line(l, str, nstr) do { \
    struct raw_line *const _l = (l); \
    const char *const _s = (str); \
    const int _n = (nstr); \
    _l->n = _n; \
    _l->s = xmalloc(_n); \
    if (_s != NULL) { \
        memcpy(_l->s, _s, _n); \
    } \
} while (0)

#define init_zero_raw_line(l) do { \
    struct raw_line *const _l = (l); \
    _l->n = 0; \
    _l->s = NULL; \
} while (0)

struct line {
    /// data of the line in utf8 format
    char *s;
    /// number of bytes on this line
    col_t n;
    /// syntax highlighting state at the beginning of the next line
    unsigned state;
    /// rendering highlights
    int *attribs;
};

/**
 * Clear the resources associated to this line.
 *
 * @param line  Line whose resources to free.
 */
#define clear_line(arg_line) do { \
    struct line *const _l = (arg_line); \
    free(_l->attribs); \
    free(_l->s); \
} while (0)

#define set_line_dirty(arg_line) do { \
    struct line *const _l = (arg_line); \
    if (_l->state != 0) { \
        _l->prev_state = _l->state; \
        _l->state = 0; \
    } \
} while (0)

#endif
