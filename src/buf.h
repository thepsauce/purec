#ifndef BUF_H
#define BUF_H

/* * * * * * * *
 *    Buffer    * * * * *
 * * * * * * * */

#include "mode.h"

#include <stdlib.h>

#include <sys/stat.h>

struct undo_event {
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

/**
 * Allocates a buffer and adds it to the buffer list.
 *
 * @param path  File path (can be `NULL` for an empty buffer)
 *
 * @return Allocated buffer.
 */
struct buf *create_buffer(const char *path);

/**
 * Deletes a buffer and removes it from the buffer list.
 *
 * @param buf   The buffer to delete.
 */
void delete_buffer(struct buf *buf);

/**
 * Insert given number of lines starting from given index.
 *
 * This function does NO clipping on `line_i`.
 *
 * @param buf       Buffer to delete within.
 * @param line_i    Line index to append to.
 * @param num_lines Number of lines to insert.
 *
 * @return First line that was inserted.
 */
struct line *insert_lines(struct buf *buf, size_t line_i, size_t num_lines);

/**
 * Delete given number of lines starting from given index.
 *
 * This function does clipping.
 *
 * @param buf       Buffer to delete within.
 * @param line_i    Inclusive line index to delete from.
 * @param num_lines Number of lines to delete
 *
 * @return 1 if anything was deleted, 0 otherwise.
 */
int delete_lines(struct buf *buf, size_t line_i, size_t num_lines);

/**
 * Deletes given inclusive range.
 *
 * This function does clipping and swapping so that `from` comes before `to`.
 *
 * @param buf   Buffer to delete within.
 * @param from  Start of deletion.
 * @param to    End of deletion.
 *
 * @return 1 if anything was deleted, 0 otherwise.
 */
int delete_range(struct buf *buf, const struct pos *from, const struct pos *to);

#endif
