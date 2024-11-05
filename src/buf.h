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
#define HUGE_UNDO_THRESHOLD 128

/**
 * This undo works by caching small text segments and writing huge text segments
 * to a file. If the `lines` variable of a segment is `NULL`, then that means
 * the contents of that segments are within the file and can be located using
 * `file_pos`. To get these lines, `seg = load_undo_data(i)` should be used and
 * then after finished, `unload_undo_data(seg)`.
 */
extern struct undo {
    /// the off memory file for large text segments
    FILE *fp;
    /// the undo data segments
    struct undo_seg {
        /// the raw string data
        char *data;
        /// length of `data`
        size_t data_len;
        /// lines within the segment
        struct line *lines;
        /// number of lines
        line_t num_lines;
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

/// whether an undo/redo should stop at this event
#define IS_STOP         0x01
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
    /// position within the buffer of the match start
    struct pos from;
    /// end position
    struct pos to;
};

#define FOPEN_PAREN 0x10000000

struct paren {
    /// position of the paranthesis within the buffer
    struct pos pos;
    /**
     * if a parenthesis is an opening one then `FOPEN_PAREN` is toggled and if
     * two paranthesis match, the lower bits match
     */
    int type;
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

    /// absolute path on the file system (can be `NULL` to signal no file)
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

    /// the indentation rule used by this buffer
    struct indent_rule rule;

    /// the text within the buffer
    struct text text;
    /// states at the start of each line
    size_t *states;
    /// attributes
    int **attribs;

    /// events that occured
    struct undo_event *events;
    /// number of events that occured
    size_t num_events;
    /// number of allocated events
    size_t a_events;
    /// current event index (1 based)
    size_t event_i;
    /// from which event the last indentation occured
    size_t ev_last_indent;

    /// all parentheses within the buffer
    struct paren *parens;
    /// number of parentheses
    size_t num_parens;
    /// number of allocated parantheses
    size_t a_parens;

    /// matches found in the buffer
    struct match *matches;
    /// number of matches in the buffer
    size_t num_matches;
    /// number of allocated matches
    size_t a_matches;
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
 * Sets the language used to highlight the buffer.
 *
 * @param buf   The buffer whose language to set.
 * @param lang  The language index into the `Langs` array.
 */
void set_language(struct buf *buf, size_t lang);

/**
 * Sets the buffer to the file contents of the buffer file, the buffer should
 * have been initialized to 0 and the buffer file should be set at this point.
 *
 * @param buf   The buffer to reload.
 *
 * @return 1 if the buffer was loaded without a file, 0 otherwise.
 */
int init_load_buffer(struct buf *buf);

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
 * Returns a pointer to a static string.
 *
 * The returned path is good for printing, do not free it.
 * This function cuts a path if it is too long.
 *
 * @return Pretty path.
 */
char *get_pretty_path(const char *path);

/**
 * Gets a buffer by an id.
 *
 * @param id    The id of the buffer.
 *
 * @return The buffer with that id or `NULL` if none exists.
 */
struct buf *get_buffer(size_t id);

/**
 * Gets the number of buffers in the linked list.
 *
 * @return The number of buffers.
 */
size_t get_buffer_count(void);

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
size_t write_file(struct buf *buf, line_t from, line_t to, FILE *fp);

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
 * Gets the number of leading blank characters of the line.
 *
 * @param buf   The buffer containing the line.
 * @param line  The index of the line.
 *
 * @return The number of leading '\t' and ' '.
 */
col_t get_nolb(struct buf *buf, line_t line);

/**
 * Gets the line indentation in space units.
 *
 * This function does NO clipping on `line_i`.
 *
 * @param buf       Buffer to look into.
 * @param line_i    Index of the line.
 * @param p_col     Destination of byte count, may be NULL.
 *
 * @return Number of leading blank characters (' ' or '\t').
 */
col_t get_line_indent(struct buf *buf, line_t line, col_t *p_col);

/**
 * Sets the indentation of a line.
 *
 * @param buf       The buffer containing the line.
 * @param line      The index of the line.
 * @param indent    The new indent in units of spaces.
 *
 * @return The event generated from indenting the line, may be NULL.
 */
struct undo_event *set_line_indent(struct buf *buf, line_t line, col_t indent);

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
struct undo_event *indent_line(struct buf *buf, line_t line_i);

/**
 * Inserts lines into the buffer without adding an event.
 *
 * @param buf    The buffer to insert lines into.
 * @param pos    The position to insert text into, must be in bound.
 * @param text   The text to insert.
 */
void insert_lines_no_event(struct buf *buf, const struct pos *pos,
                           const struct text *text);

/**
 * Insert lines starting from a given position.
 *
 * All added lines are initialized to given lines and an event is added.
 *
 * @param buf       Buffer to insert in.
 * @param pos       Position to append to, must be in bound.
 * @param text      The text to insert, must have some text.
 * @param repeat    How many times to repeat the insertion, must be > 0.
 *
 * @return The event generated from this insertion (may be `NULL`).
 */
struct undo_event *insert_lines(struct buf *buf, const struct pos *pos,
                                const struct text *text, size_t repeat);

/**
 * Insert given number of lines starting from given position.
 *
 * @param buf       Buffer to insert in.
 * @param pos       Position to append to.
 * @param text      The text to insert, must be non empty; this
 *                  function takes ownership of the text object.
 *
 * @return The event generated from the insertion.
 */
struct undo_event *_insert_lines(struct buf *buf, const struct pos *pos,
                                 struct text *text);

/**
 * Inserts given text block without adding an event.
 *
 * @param buf    The buffer to insert the block into.
 * @param pos    The position to insert the text into, must be in bouind.
 * @param text   The text to insert as block, must be non empty.
 */
void insert_block_no_event(struct buf *buf, const struct pos *pos,
                           const struct text *text);

/**
 * Insert lines in block mode starting from a given position.
 *
 * All added lines are initialized to given lines and an event is added.
 *
 * @param buf       Buffer to insert in.
 * @param pos       Position to append to, must be in bound.
 * @param text      The text ot insert as block.
 * @param repeat    How many times to repeat the insertion, must be greater 0.
 *
 * @return The event generated from this insertion (may be `NULL`).
 */
struct undo_event *insert_block(struct buf *buf, const struct pos *pos,
                                const struct text *text, size_t repeat);

/**
 * Insert lines in block mode starting from a given position.
 *
 * @param buf       Buffer to insert in.
 * @param pos       Position to append to, must be in bound.
 * @param text      Text to insert as block, must be non empty.
 */
struct undo_event *_insert_block(struct buf *buf, const struct pos *pos,
                                 struct text *text);

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
 * Gets the first index of the match on the given line.
 *
 * @param buf       The buffer to look for the match.
 * @param line_i    The line to look for.
 *
 * @return The index of the match.
 */
size_t get_match_line(struct buf *buf, line_t line_i);

/**
 * Called after inserting lines into `buf->text` to update all other data
 * structures.
 *
 * @param buf       Buffer to make it notice.
 * @param line_i    Line index the insertion took place.
 * @param num_lines Number of lines inserted.
 */
void notice_line_growth(struct buf *buf, line_t line_i, line_t num_lines);

/**
 * Called after deleting lines from `buf->text`.
 *
 * @param buf       The buffer to insert text into.
 * @param line_i    The index to the first line deleted.
 * @param num_lines The number of lines deleted.
 */
void notice_line_removal(struct buf *buf, line_t line_i, line_t num_lines);

/**
 * Deletes given range without adding an event.
 *
 * @param buf    The buffer to delete text out of.
 * @param from   The start of the deletion, must be in boudn.
 * @param to     The end of the deletion, must be in bound.
 */
void delete_range_no_event(struct buf *buf,
                           const struct pos *from,
                           const struct pos *to);

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
struct undo_event *delete_range(struct buf *buf,
                                const struct pos *from,
                                const struct pos *to);

/**
 * Deletes a block out of given buffer without adding an event.
 *
 * @param buf    The buffer to delete text out of.
 * @param from   The upper left corner of the block.
 * @param to     The lower right corner of the block.
 */
void delete_block_no_event(struct buf *buf,
                           const struct pos *from,
                           const struct pos *to);

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
struct undo_event *delete_block(struct buf *buf,
                                const struct pos *from,
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
void _delete_block(struct buf *buf,
                   const struct pos *from,
                   const struct pos *to);

/**
 * Deletes given range and inserts given lines.
 *
 * This function adds one or two events, an insertion or deletion event and
 * a replace event if applicable.
 *
 * @param buf       Buffer to replace lines.
 * @param from      Start of deletion.
 * @param to        End of deletion.
 * @param text      The text to insert.
 *
 * @return The first added event.
 */
struct undo_event *replace_lines(struct buf *buf,
                                 const struct pos *from,
                                 const struct pos *to,
                                 const struct text *text);

/**
 * Deletes given range and inserts given lines.
 *
 * This function adds one or two events, an insertion or deletion event and
 * a replace event if applicable.
 *
 * @param buf       Buffer to replace lines.
 * @param from      Start of deletion.
 * @param to        End of deletion.
 * @param text      The text to insert, ownership of this pointer is taken away.
 *
 * @return The first added event.
 */
struct undo_event *_replace_lines(struct buf *buf,
                                 const struct pos *from,
                                 const struct pos *to,
                                 struct text *text);

/// outside parameter for `conv_to_char`
extern int ConvChar;

/**
 * Meant to be passed in to the change routines.
 */
int conv_to_char(int c);

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
struct undo_seg *save_lines(struct text *text);

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
                             struct text *text);

/**
 * Undoes an event but ignores the transient flag.
 *
 * @param buf   Buffer to undo in.
 *
 * @return The event undone or `NULL` if there was none.
 */
struct undo_event *undo_event_no_trans(struct buf *buf);

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
 * Searches for a pattern within a buffer.
 * The result of this function is stored within the buffer itself.
 *
 * @param buf   The buffer to search in.
 * @param pat   The pattern to look for.
 *
 * @return The number of matches.

 */
size_t search_pattern(struct buf *buf, const char *pat);

/**
 * Rehighlights given lines.
 *
 * @param buf       The buffer containing the line.
 * @param line_i    The line to highlight again.
 * @param num_lines The number of lines starting from `line_i`.
 */
void rehighlight_lines(struct buf *buf, line_t line_i, line_t num_lines);

/**
 * Gets the index where the given position should be inserted within the
 * paranthesis list.
 *
 * @param buf   The buffer whose paranthesis list to use.
 * @param pos   The position of the not yet included parenthesis.
 *
 * @return The index where to insert it.
 */
size_t get_next_paren_index(const struct buf *buf, const struct pos *pos);

/**
 * Adds the given position as parenthesis.
 *
 * @param buf   The buffer to add a parenthesis to.
 * @param pos   The position of the parenthesis.
 * @param type  The type of the parenthesis.
 */
void add_paren(struct buf *buf, const struct pos *pos, int type);

/**
 * Gets the parenthesis at given position.
 *
 * @param buf   The buffer to get the parenthesis from.
 * @param pos   The position of the parenthesis.
 *
 * @return The index of the parenthesis within the buffer parenthesis list or
 *         `SIZE_MAX` on failure.
 */
size_t get_paren(struct buf *buf, const struct pos *pos);

/**
 * Removes all parentheses on a line.
 *
 * @param buf       The buffer whose parenthesis data to modify.
 * @param line_i    The index of the line.
 */
void clear_parens(struct buf *buf, line_t line_i);

/**
 * Gets the matching parenthesis within the buffer.
 *
 * @param buf       The buffer to get the matching parenthesis in.
 * @param paren_i   The index of the parenthesis to find a match for.
 *
 * @return The matching parenthesis or `SIZE_MAX` if none was found.
 */
size_t get_matching_paren(struct buf *buf, size_t paren_i);

#endif
