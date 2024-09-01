#ifndef BUF_H
#define BUF_H

/* * * * * * * *
 *    Buffer    * * * * *
 * * * * * * * */

#include "purec.h"

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <sys/stat.h>

/// at which number of bytes to start writing to the undo file
#define HUGE_UNDO_THRESHOLD 64

/**
 * This undo works by caching small text segments and writing huge text segments
 * to a file. If the `lines` variable of a segment is `NULL`, then that means
 * the contents of that segments are within the file and can be located using
 * `file_pos`. To get these lines, `load_undo_data(i)` should be used and then
 * after finished, `unload_undo_data(i)`.
 */
extern struct undo {
    /// the off memory file for large text segments
    FILE *fp;
    struct undo_seg {
        /// the raw string data
        char *data;
        /// length of `data`
        size_t data_len;
        /// lines within the segment
        struct raw_line *lines;
        /// number of lines
        size_t num_lines;
        /// position within the file `fp`
        fpos_t file_pos;
        /// how many times this segment was loaded without unloading
        size_t load_count;
    } **segments;
    /// number of undo segments
    size_t num_segments;
    /// number of allocated undo segments
    size_t a_segments;
} Undo;

/// whether the next event should be processed together with this one
#define IS_TRANSIENT    0x01
/// whether the event changes a block rather than a range
#define IS_BLOCK        0x02
/// if this is an insertion event
#define IS_INSERTION    0x04
/// if this is a deletion event
#define IS_DELETION     0x08
/// if this is a replace event
#define IS_REPLACE      0x10

/**
 * The `lines` value depends on the type of event. For a deletion event, it is
 * the deleted text, for an insertion event it is the inserted text and for a
 * replace event, it is a XOR of the changed text.
 *
 * Note: The `undo_cur` and `redo_cur` of each event transient chain is only
 * valid for the first and last event respectively in a transient chain.
 * A transient chain start with an event that has the transient flag on and
 * ends at the first event without the transient flag.
 */
struct undo_event {
    /// flags of this event
    int flags;
    /// time of the event
    time_t time;
    /// position of the event
    struct pos pos;
    /// the position afther the event
    struct pos end;
    /// cursor position before the event
    struct pos cur;
    /// the data segment
    struct undo_seg *seg;
};

struct match {
    /// position the match starts from
    struct pos from;
    /// end position
    struct pos to;
};

/**
 * After buffer creation, it is guaranteed that `num_lines` will always be at
 * least 1 and never 0.
 *
 * WARNING: Using any of event returned by a buffer editing function can only be
 * done while no other function is called in between. Example:
 * ```C
 * struct buf *buf = ...;
 * struct undo_event *ev;
 * struct pos pos = { 0, 0 };
 * struct undo_seg *seg;
 *
 * ev = break_line(buf, &pos);
 * ev->cur.line = 1; // <-- Safe
 * ev->cur.col = 0; // <-- Safe
 *
 *                     //! UNSAFE !//
 * (void) indent_line(buf, &ev->cur);
 * seg = ev->seg; //! UNSAFE !//
 * ```
 */
struct buf {
    /**
     * Unique identifier of this buffer. The pointer returned by
     * `create_buffer()` is also a unique identifier, however, the ID is meant
     * to be human redable and easy to type, it is never 0.
     */
    size_t id;

    /// path on the file system (can be `NULL` to signal no file)
    char *path;
    /// last statistics of the file
    struct stat st;
    /// the event index at the time of saving
    size_t save_event_i;

    /// saved cursor position
    struct pos save_cur;
    /// saved scrolling
    struct pos save_scroll;

    /// the index of the highlighting machine
    size_t lang;

    /// the first dirty line
    size_t min_dirty_i;
    /// the last dirty line; there might also be dirty lines in the middle
    size_t max_dirty_i;

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

    /// matches found in the buffer
    struct match *matches;
    /// number of matches in the buffer
    size_t num_matches;
    /// last search pattern
    char *search_pat;

    /// next buffer in the buffer linked list
    struct buf *next;
};

/**
 * The first buffer in the linked list. The linked list is always sorted in
 * ascending order with respect to the buffer ID.
 */
extern struct buf *FirstBuffer;

/**
 * Allocates a buffer and adds it to the buffer list.
 *
 * @param path  File path (can be `NULL` for an empty buffer)
 *
 * @return The allocated buffer.
 */
struct buf *create_buffer(const char *path);

/**
 * Tries to make a guess what language is within the buffer.
 *
 * @param buf   The buffer to detect the languag within.
 *
 * @return The detected language.
 */
size_t detect_language(struct buf *buf);

/**
 * Sets the buffer to the file contents of the buffer file, the buffer should
 * have been initialized to 0 and the buffer file should be set at this point.
 *
 * @param buf   The buffer to reload.
 */
void init_load_buffer(struct buf *buf);

/**
 * Deletes a buffer and removes it from the buffer list.
 *
 * When deleting a buffer, there will be a gap in the IDs, so the next buffer
 * that is created will get the ID that this buffer used to have.
 *
 * @param buf   The buffer to destroy.
 */
void destroy_buffer(struct buf *buf);

/**
 * Gets a buffer by and id.
 *
 * @param id    The id of the buffer.
 *
 * @return The buffer with that id or `NULL` if none exists.
 */
struct buf *get_buffer(size_t id);

void set_language(struct buf *buf, size_t lang);

/**
 * Gets the number of buffers in the linked list.
 *
 * @return The number of buffers.
 */
size_t get_buffer_count(void);

void set_language(struct buf *buf, size_t lang);

/**
 * Writes lines from a buffer to a file.
 *
 * Note: If `from` is greater than `to` or greater than the number of lines the
 * buffer has, nothing is written. `from` and `to` are clipped to the last line.
 *
 * @param buf   The buffer to take lines from.
 * @param from  The first line to write.
 * @param to    The last line to write.
 * @param fp    The file to write to.
 *
 * @return The number of bytes written.
 */
size_t write_file(struct buf *buf, size_t from, size_t to, FILE *fp);

/**
 * NOTE: The below functions that return a `struct undo_event *` do not set the
 * `cur_undo` and `cur_redo` values, they must be set by the caller.
 */

/**
 * Reads a file into the buffer.
 *
 * @param buf   The buffer to read into.
 * @param pos   The position to append from.
 * @param fp    The file to read from.
 *
 * @return The event generated by inserting the file.
 */
struct undo_event *read_file(struct buf *buf, const struct pos *pos, FILE *fp);

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
 * Sets the `min_dirty_i` and `max_dirty_i` values within a buffer.
 *
 * Implementation:
 * ```C
 * buf->min_dirty_i = MIN(buf->min_dirty_i, from);
 * buf->max_dirty_i = MAX(buf->max_dirty_i, to);
 * ```
 *
 * @param buf   The buffer to set values in.
 * @param from  The start of the range.
 * @param to    The end of the range.
 */
void update_dirty_lines(struct buf *buf, size_t from, size_t to);

/**
 * Insert lines starting from a given position.
 *
 * All added lines are initialized to given lines and an event is added.
 *
 * WARNING: This function does NO clipping on `pos`.
 *
 * @param buf       Buffer to insert in.
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
 * @param buf       Buffer to insert in.
 * @param pos       Position to append to.
 * @param num       Lines to insert.
 * @param num_lines Number of lines to insert.
 */
void _insert_lines(struct buf *buf, const struct pos *pos,
        const struct raw_line *lines, size_t num_lines);

/**
 * Insert lines in block mode starting from a given position.
 *
 * All added lines are initialized to given lines and an event is added.
 *
 * WARNING: This function does NO clipping on `pos`.
 *
 * @param buf       Buffer to insert in.
 * @param pos       Position to append to.
 * @param lines     Lines to insert.
 * @param num_lines Number of lines to insert.
 * @param repeat    How many times to repeat the insertion.
 *
 * @return The event generated from this insertion (may be `NULL`).
 */
struct undo_event *insert_block(struct buf *buf, const struct pos *pos,
        const struct raw_line *lines, size_t num_lines, size_t repeat);

/**
 * Insert lines in block mode starting from a given position.
 *
 * WARNING: This function does NO clipping on `pos`.
 *
 * @param buf       Buffer to insert in.
 * @param pos       Position to append to.
 * @param lines     Lines to insert.
 * @param num_lines Number of lines to insert.
 */
void _insert_block(struct buf *buf, const struct pos *pos,
        const struct raw_line *lines, size_t num_lines);

/**
 * Breaks the line at given position by inserting '\n' and indents the line
 * according to the current indentation rules.
 *
 * WARNING: This function does NO clipping on `pos`.
 *
 * @param buf   Buffer to break a line within.
 * @param pos   Position to break a line at.
 *
 * @return The event generated from breaking the line.
 */
struct undo_event *break_line(struct buf *buf, const struct pos *pos);

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
 * Gets the lines within the buffer inside a given range.
 *
 * @param buf   Buffer to get lines from.
 * @param from  Start of the range.
 * @param to    End of the range.
 *
 * @return Allocated lines, `p_num_lines` has the number of lines.
 */
struct raw_line *get_lines(struct buf *buf, const struct pos *from,
        const struct pos *to, size_t *p_num_lines);

/**
 * Gets a block from within a buffer.
 *
 * @param buf   Buffer to get the block from.
 * @param from  Upper left corner of the block.
 * @param to    Lower right corner of the block.
 *
 * @return Allocated lines, `p_num_lines` has the number of lines.
 */
struct raw_line *get_block(struct buf *buf, const struct pos *from,
        const struct pos *to, size_t *p_num_lines);

/**
 * Deletes a range from a buffer.
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
 * Same as delete_range() but no clipping and no event adding.
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
 * Deletes given inclusive block.
 *
 * This function does NO clipping.
 *
 * @param buf   Buffer to delete within.
 * @param from  Start of deletion.
 * @param to    End of deletion.
 */
void _delete_block(struct buf *buf, const struct pos *from,
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
 * Checks whether it makes sense to join the two given events.
 *
 * Note that `ev1` must be an event that happened right before `ev2`.
 *
 * @param ev1   The first event.
 * @param ev2   The second event.
 *
 * @return Whether it makes sense to join the events.
 */
bool should_join(const struct undo_event *ev1, const struct undo_event *ev2);

/**
 * Saves given lines.
 *
 * @param lines     The lines to save.
 * @param num_lines The number of lines to save.
 *
 * @return Allocated data segment.
 */
struct undo_seg *save_lines(struct raw_line *lines, size_t num_lines);

/**
 * Loads the data of the given data segment.
 *
 * @param seg   The data segment to load.
 */
void load_undo_data(struct undo_seg *seg);

/**
 * Cleans up after a call to `load_undo_data()`.
 *
 * @param seg   The undo data segment.
 */
void unload_undo_data(struct undo_seg *seg);

/**
 * Adds an event to the buffer event list.
 *
 * This sets the `time` value of the event to the current time in seconds.
 *
 * @param buf       Buffer to add an event to.
 * @param flags     The flags of the event.
 * @param lines     The lines of the event, must be allocated on the heap.
 * @param num_lines The number of lines.
 *
 * @return The event that was added.
 */
struct undo_event *add_event(struct buf *buf, int flags, const struct pos *pos,
        struct raw_line *lines, size_t num_lines);

/**
 * Undoes an event.
 *
 * @param buf   Buffer to undo in.
 *
 * @return The event undone or `NULL` if there was none.
 */
struct undo_event *undo_event(struct buf *buf);

/**
 * Redoes an undone event.
 *
 * @param buf   Buffer to redo in.
 *
 * @return The event redone or `NULL` if there was none.
 */
struct undo_event *redo_event(struct buf *buf);

/**
 * Performs the action given event defines.
 *
 * This function may return `NULL` when the action cannot be performed because
 * or an out of bounds deletion.
 *
 * This function does NOT check if the position given in the event is in bound.
 * It also does not support replace events.
 *
 * @param buf   The buffer to perform the action in.
 * @param ev    The action to perform.
 *
 * @return The event generated from performing the action.
 */
struct undo_event *perform_event(struct buf *buf, const struct undo_event *ev)
    __attribute__((deprecated));

/**
 * Searches a string within a buffer.
 *
 * The result of this function is stored within the buffer itself.
 *
 * @param buf   The buffer to search in.
 * @param s     The string to search for.
 *
 * @return The number of matches.
 */
size_t search_string(struct buf *buf, const char *s);

#endif
