#ifndef LINE_H
#define LINE_H

#include <stdlib.h>

#include <ncurses.h>

/// if the line has a gdb breakpoint
#define LINE_BREAKPOINT 0x1

struct attr {
    /// color pair
    int cp;
    /// render attributes
    attr_t a;
    /// conceal
    const char *cc;
};

struct line {
    /// special flags (`LINE_*`)
    int flags;
    /// syntax highlighting state at the beginning of the next line
    size_t state;
    /// rendering attributes
    struct attr *attribs;
    /// data of the line
    char *s;
    /// number of bytes on this line
    size_t n;
};

/**
 * Mark the line as dirty by setting `state` to 0.
 *
 * @param line  Line to mark as dirty.
 */
#define mark_dirty(arg_line) do { \
    struct line *const _l = (arg_line); \
    _l->state = 0; \
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
