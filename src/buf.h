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

/// whether the next event should be process together with this one
#define IS_TRANSIENT    0x1
/// if this is an insertion event
#define IS_INSERTION    0x2
/// if this is a deletion event
#define IS_DELETION     0x4
/// if this is a replace event
#define IS_REPLACE      0x8

struct undo_event {
    /// flags of this event
    int flags;
    /// time of the event
    time_t time;
    /// position of the event
    struct pos pos;
    /// cursor position before the event
    struct pos undo_cur;
    /// cursor position after the event
    struct pos redo_cur;
    /// changed lines
    struct raw_line *lines;
    /// number of changed lines
    size_t num_lines;
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
    /// the event index at the time of saving
    size_t save_event_i;

    /// lines within this buffer
    struct line *lines;
    /// number of lines
    size_t num_lines;
    /// number of allocated lines
    size_t a_lines;

    /// events that occured
    struct undo_event **events;
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
 * Writes given buffer to given file or the buffer path if file is `NULL`.
 *
 * @param buf   The buffer to write.
 * @param file  The file to write to.
 *
 * @return Whether the file could be opened.
 */
int write_file(struct buf *buf, const char *file);


/**
 * Reads given file into the buffer.
 *
 * @param buf   The buffer to read into.
 * @param pos   The position to append from.
 * @param file  The file to read from.
 *
 * @return The event generated by inserting the file.
 */
struct undo_event *read_file(struct buf *buf, const struct pos *pos,
        const char *file);

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
 * Insert given number of lines starting from given position.
 *
 * All added lines are initialized to given lines or 0 if they are `NULL` and
 * an event is added.
 *
 * WARNING: This function does NO clipping on `pos`.
 *
 * @param buf       Buffer to delete within.
 * @param pos       Position to append to.
 * @param lines     Lines to insert.
 * @param num_lines Number of lines to insert.
 * @param repeat    How many times to repeat the insertion.
 *
 * @return The event generated from this insertion (may be `NULL`).
 */
struct undo_event *insert_lines(struct buf *buf, const struct pos *pos,
        const struct raw_line *lines, size_t num_lines, size_t repeat);

/**
 * Insert given number of lines starting from given position.
 *
 * WARNING: This function does NO clipping on `pos`.
 *
 * @param buf       Buffer to delete within.
 * @param pos       Position to append to.
 * @param num       Lines to insert.
 * @param num_lines Number of lines to insert.
 */
void _insert_lines(struct buf *buf, const struct pos *pos,
        const struct raw_line *lines, size_t num_lines);

/**
 * Insert given number of lines starting from given index.
 *
 * This simply inserts uninitialized lines after given index. NO clipping and NO
 * adding of an event.
 *
 * @param buf       Buffer to delete within.
 * @param line_i    Line index to append to.
 * @param num_lines Number of lines to insert.
 *
 * @return First line that was inserted.
 */
struct line *grow_lines(struct buf *buf, size_t line_i, size_t num_lines);

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
 * Deletes given range.
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
 * This marks the changed lines as dirty.
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

/**
 * Deletes given inclusive block.
 *
 * This function does clipping and swapping so that `from` comes before `to`.
 * The operation is added as event.
 *
 * Example deleting the first character of all lines:
 * ```C
 * struct buf *buf = ...;
 * struct pos from = { 0, 0 };
 * struct pos to = { buf->num_lines, 0 };
 * delete_block(buf, &from, &to);
 * ```
 *
 * @param buf   Buffer to delete within.
 * @param from  Start of deletion.
 * @param to    End of deletion.
 *
 * @return The event generated from this deletion (may be `NULL`).
 */
struct undo_event *delete_block(struct buf *buf, const struct pos *from,
        const struct pos *to);

/**
 * Change text within a block.
 *
 * @param buf  Buffer of which to change text in.
 * @param from Start of changing.
 * @param to   End of changing.
 * @param conv The conversion function for characters.
 *
 * @result The event generated by changing.
 */
struct undo_event *change_block(struct buf *buf, const struct pos *from,
        const struct pos *to, int (*conv)(int));

/**
 * Change text within a range.
 *
 * @param buf  Buffer of which to change case in.
 * @param from Start of case changing.
 * @param to   End of case changing.
 * @param conv The conversion function for characters.
 *
 * @result The event generated by changing.
 */
struct undo_event *change_range(struct buf *buf, const struct pos *from,
        const struct pos *to, int (*conv)(int));

/**
 * Below functions are implemented in undo.c.
 */

/**
 * Frees given event and its data.
 *
 * @param ev    The event to free.
 */
void free_event(struct undo_event *ev);

/**
 * Checks whether it makes sense to join the two given events.
 *
 * Note that `ev1` must be an event that happened right before `ev2`.
 *
 * @param ev1   The first event.
 * @param ev2   The second event.
 *
 * @return Whether it makes sense to join the events.
 */
bool should_join(struct undo_event *ev1, struct undo_event *ev2);

/**
 * Adds an event to the buffer event list.
 *
 * This sets the `time` value of the event to the current time in seconds.
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

#endif
