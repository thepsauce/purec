#ifndef LINE_H
#define LINE_H

#include <stdlib.h>

#include <ncurses.h>

struct raw_line {
    /// data of the line
    char *s;
    /// number of bytes on this line
    size_t n;
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
    const size_t _n = (nstr); \
    _l->n = _n; \
    _l->s = xmalloc(_n); \
    if (_s != NULL) { \
        memcpy(_l->s, _s, _n); \
    } \
} while (0)

/// if the line has a gdb breakpoint
#define LINE_BREAKPOINT 0x1

struct attr {
    /// color pair
    int cp;
    /// render attributes
    attr_t a;
};

struct line {
    /// data of the line
    char *s;
    /// number of bytes on this line
    size_t n;
    /// special flags (`LINE_*`)
    int flags;
    /// syntax highlighting state at the beginning of the next line
    unsigned state;
    /// state before the line was marked dirty
    unsigned prev_state;
    /// rendering attributes
    struct attr *attribs;
};

/**
 * Mark the line as dirty by setting `state` to 0.
 *
 * @param line  Line to mark as dirty.
 */
#define mark_dirty(arg_line) do { \
    struct line *const _l = (arg_line); \
    if (_l->state != 0) { \
        _l->prev_state = _l->state; \
        _l->state = 0; \
    } \
} while (0)

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

#endif
