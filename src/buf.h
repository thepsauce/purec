#ifndef BUF_H
#define BUF_H

/* * * * * * * *
 *    Buffer    * * * * *
 * * * * * * * */

#include "mode.h"

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <sys/stat.h>

/* TODO: Find a way to join events by combining their data structures,
 * this would improve efficiency and make `flags` obsolete.
 */
/**
 * Joins the events in the event list starting from `index`.
 *
 * @param buf   The buffer whose event list to use.
 * @param ev    Event in the event list to start from.
 *
 * @return The event itself (`ev`).
 */
//struct undo_event *join_events(struct buf *buf, struct undo_event *ev);

struct undo_event {
    /// whether the next event should be process together with this one
    bool is_transient;
    /// time of the event
    time_t time;
    /// position of the event
    struct pos pos;
    /// cursor position before the event
    struct pos undo_cur;
    /// cursor position after the event
    struct pos redo_cur;
    /// number of bytes inserted
    size_t ins_len;
    /// number of bytes deleted
    size_t del_len;
    /// changed text
    char *text;
};

/**
 * After buffer creation, it is guaranteed that `num_lines` will always be at
 * least 1 and never 0.
 */
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
    /// current event index (1 based)
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
 * Adds an event to the buffer event list.
 *
 * This sets the `time` value of the event to the current time in seconds. It
 * also sets `is_transient` to false.
 *
 * @param buf   Buffer to add an event to.
 * @param ev    Event to add.
 *
 * @return The event that was added.
 */
struct undo_event *add_event(struct buf *buf, const struct undo_event *ev);

/**
 * Undo an event.
 *
 * @param buf   Buffer to undo in.
 *
 * @return The event undone or `NULL` if there was none.
 */
struct undo_event *undo_event(struct buf *buf);

/**
 * Redo an undone event.
 *
 * @param buf   Buffer to redo in.
 *
 * @return The event redone or `NULL` if there was none.
 */
struct undo_event *redo_event(struct buf *buf);

/**
 * Gets the line indentation in bytes.
 *
 * This function does NO clipping on `line_i`.
 *
 * @param buf       Buffer to look into.
 * @param line_i    Index of the line.
 *
 * @return Number of leading blank characters (' ' or '\t').
 */
size_t get_line_indent(struct buf *buf, size_t line_i);

/**
 * Insert given number of lines starting from given index.
 *
 * If `line_i` is out of bounds, it is set to the last line. All added lines are
 * initialized to 0 and no event is added. This does NOT add an event.
 *
 * @param buf       Buffer to delete within.
 * @param line_i    Line index to append to.
 * @param num_lines Number of lines to insert.
 *
 * @return First line that was inserted.
 */
struct line *insert_lines(struct buf *buf, size_t line_i, size_t num_lines);

/**
 * Insert given number of lines starting from given index.
 *
 * Behaves similar to `insert_lines()` but does do out of bounds checking nor
 * initialization of the added lines.
 *
 * @param buf       Buffer to delete within.
 * @param line_i    Line index to append to.
 * @param num_lines Number of lines to insert.
 *
 * @return First line that was inserted.
 *
 * @see insert_lines()
 */
struct line *_insert_lines(struct buf *buf, size_t line_i, size_t num_lines);

/**
 * NOTE: The below functions that return a `struct undo_event *` do not set the
 * `cur_undo` and `cur_redo` values, they must be set by the caller.
 */

/**
 * Indents the line at `line_i`.
 *
 * This inserts spaces at the front of the line until it "seems" indented. It
 * also adds an event.
 *
 * @param buf       Buffer to indent the line in.
 * @param line_i    Index of the line to indent.
 *
 * @return Event generated from adding/removing spaces at the front of the line.
 */
struct undo_event *indent_line(struct buf *buf, size_t line_i);

/**
 * Inserts text at given position.
 *
 * This functions adds an event but does NO clipping. It checks however if
 * `len_text * repeat` overflows and reduces `repeat` until it fits into a
 * `size_t`.
 *
 * To check beforehand if the operation would overflow, try:
 * ```
 * if (safe_mul(len_text, repeat) == SIZE_MAX) {
 *     OVERFLOW IF len_text != 1 && repeat != 1
 * }
 * ```
 * or:
 * ```
 * size_t prod;
 * if (__builtin_overflow(len_text, repeat, &prod)) {
 *     OVERFLOW
 * }
 * ```
 *
 * @param buf       Buffer to insert text in.
 * @param pos       Position to insert text from.
 * @param text      Text to insert.
 * @param repeat    How many times to insert the text.
 * @param len_text  Length of the text to insert.
 *
 * @return The event generated from this insertion (may be `NULL`).
 */
struct undo_event *insert_text(struct buf *buf, struct pos *pos,
        const char *text, size_t len_text, size_t repeat);

/**
 * Inserts text at given position.
 *
 * This functions adds NO event and does NO clipping. It also does not check if
 * the product of `len_text` and `repeat` overflow.
 *
 * @param buf       Buffer to insert text in.
 * @param pos       Position to insert text from.
 * @param text      Text to insert.
 * @param repeat    How many times to insert the text.
 * @param len_text  Length of the text to insert.
 */
void _insert_text(struct buf *buf, struct pos *pos,
        const char *text, size_t len_text, size_t repeat);

/**
 * Deletes given inclusive range.
 *
 * This function does clipping and swapping so that `from` comes before `to`.
 * The operation is added as event.
 *
 * Example deleting the entire text of a buffer:
 * ```C
 * struct buf *buf = ...;
 * struct pos from = { 0, 0 };
 * struct pos to = { buf->num_lines, 0 };
 * delete_range(buf, &from, &to);
 * ```
 *
 * @param buf   Buffer to delete within.
 * @param from  Start of deletion.
 * @param to    End of deletion.
 *
 * @return The event generated from this deletion (may be `NULL`).
 *
 * @see _delete_range()
 */
struct undo_event *delete_range(struct buf *buf, const struct pos *from,
        const struct pos *to);

/**
 * Same as delete_range() but no clipping, no event adding.
 *
 * This function works in an excluseive way. If `*pfrom == *pto` is true, this
 * is already undefined behaviour as this function EXPECTS that there is
 * something to delete.
 *
 * Example:
 * ```
 * struct buf *buf = ...;
 * struct pos from = { 3, 2 };
 * struct pos to = { 4, 1 };
 * _delete_range(buf, &from, &to);
 * ```
 * This deletes the line at index 4 and moves the rest of line index 4 into line
 * index 3. The first two characters of line index 3 are deleted as well.
 *
 * @param buf   Buffer to delete within.
 * @param from  Start of deletion.
 * @param to    End of deletion.
 *
 * @see delete_range()
 */
void _delete_range(struct buf *buf, const struct pos *pfrom, const struct pos *pto);

#endif
