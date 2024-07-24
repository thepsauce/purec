#ifndef BUF_H
#define BUF_H

#include "mode.h"

#include <stdlib.h>

#include <sys/stat.h>

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

/// text was inserted (`text` is the inserted text)
#define UNDO_INSERT 1
/// text was deleted (`text` is the deleted text)
#define UNDO_DELETE 2
/// text was replaced (`text` is the xor of the text plus additional bytes)
#define UNDO_REPLACE 3

struct undo_event {
    /// type of the event (`UNDO_*`)
    int type;
    /// position of the event
    struct pos pos;
    /// changed text
    char *text;
    /// number of bytes inserted
    size_t ins_len;
    /// number of bytes deleted
    size_t del_len;
};

struct buf {
    /// path on the file system (can be `NULL` to signal no file)
    char *path;
    /// last statistics of the file
    struct stat st;

    /// lines within this buffer
    struct line *lines;
    /// number of lines
    size_t num_lines;
    /// number of allocated lines
    size_t a_lines;

    /// events that occured
    struct undo_event *events;
    /// number of ecents that occured
    size_t num_events;
    /// current event index
    size_t event_i;
};

struct buf *create_buffer(const char *path);
void delete_buffer(struct buf *buf);

#endif
