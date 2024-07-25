#ifndef LINE_H
#define LINE_H

#include <stdlib.h>

/// if the line has a gdb breakpoint
#define LINE_BREAKPOINT 0x1
/// if the line is hidden
#define LINE_HIDDEN 0x2
/// if the line should be ignored
#define LINE_IGNORE 0x4

struct line {
    /// special flags (`LINE_*`)
    int flags;
    /// data of the line
    char *s;
    /// number of bytes on this line
    size_t n;
    /// starting from this line, how many lines should be inserted extra
    size_t inc;
};

#endif
