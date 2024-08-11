#ifndef MODE_H
#define MODE_H

/* ** *** **** *** ** *
 *        Mode        *
 * ** *** **** *** ** */

#include "line.h"
#include "util.h"

/**
 * Message that is rendered at the bottom of the screen.
 */
extern char *Message;

/**
 * Formats the message like `printf()` and puts it into `Message`.
 *
 * @param fmt   Format string.
 * @param ...   Format arguments.
 */
void format_message(const char *fmt, ...);

/// Whether the editor should continue.
extern bool IsRunning;

/// The exit code to return in `main()`.
extern int ExitCode;

#define NORMAL_MODE 0 /* 0 */
#define INSERT_MODE 1 /* 1 */

#define IS_VISUAL(mode) (!!((mode)&2))
#define VISUAL_MODE 2 /* 2 */
#define VISUAL_LINE_MODE (2|1) /* 3 */
#define VISUAL_BLOCK_MODE (2|4) /* 6 */

/**
 * The mode struct contains information about all modes.
 */
extern struct mode {
    /// type of the mode (`*_MODE`)
    int type;
    /// additional counter
    size_t extra_counter;
    /// counter
    size_t counter;
    /// saved cursor positon (for visual mode and multi line insertion)
    struct pos pos;
    /// normal mode
    struct normal_mode {
        /* TODO: move jumps */
        struct pos *jumps;
        size_t num_jumps;
        size_t jump_i;
    } normal;
    /// how many times the last insertion should be repeated
    size_t repeat_count;
    /// event from which the last insert mode started
    size_t ev_from_ins;
    /// number of lines to add text to
    size_t num_dup;
} Mode;

struct selection {
    /// if it is a block selection
    bool is_block;
    /// beginning of the selection
    struct pos beg;
    /// end of the selection
    struct pos end;
};

/**
 * Multiplies the counter by 10 and adds given number to it.
 *
 * When it would overflow, the counter is set to `SIZE_MAX` instead.
 *
 * @param d Number to add (best between 0 and 9 (inclusive))
 */
void shift_add_counter(int d);

/**
 * Gets user input like normal but handle digits in a special way.
 *
 * If the input is '0' and the counter is 0, then '0' is returned but when the
 * counter is non zero, then the counter is multiplied by 10 and the next
 * character is read. For any other digit ('1' to '9') it is appended to the
 * counter and the next character is retrieved.
 *
 * @return Next input character.
 */
int getch_digit(void);

/**
 * Gets the end of a line which is different in insert and normal mode.
 *
 * In normal mode the line is index based which means the caret can not go right
 * after the end of the line.
 *
 * @param line  Line to get the end of.
 *
 * @return Index of the line ending.
 */
size_t get_mode_line_end(struct line *line);

/**
 * Gets the corrected counter.
 *
 * @param counter   Counter value.
 *
 * @return 1 if the counter 0, otherwise the counter.
 */
size_t correct_counter(size_t counter);

/**
 * Sets the new mode and does any transition needed like changing cursor shape.
 *
 * If the current mode is not a visual mode but the new mode is, `Mode.pos` is
 * set to the cursor position within the current frame (`SelFrame`). This is
 * used to render the selection and do actions upon it.
 *
 * @param mode  The new mode.
 */
void set_mode(int mode);

/**
 * Gets the selection of the current frame.
 *
 * This simply gets the cursor position within the current frame and `Mode.pos`
 * and sorts them, there is additional correction when the visual line mode is
 * active. When the visual block mode is active, this is a block selection and
 * `is_block` is set to true.
 *
 * @param sel   Result.
 *
 * @return Whether there is a selection.
 */
bool get_selection(struct selection *sel);

/**
 * Checks whether the given point is within the given selection.
 *
 * @param sel   The selection to check.
 * @param pos   The point to check.
 *
 * @return Whether the point is within the selection.
 */
bool is_in_selection(const struct selection *sel, const struct pos *pos);

/**
 * Handles a key input for the normal mode.
 *
 * @return Whether the ui needs to be updated.
 */
int normal_handle_input(int c);

/**
 * Handles a key input for the insert mode.
 *
 * @return Whether the ui needs to be updated.
 */
int insert_handle_input(int c);

/**
 * Handles a key input for the visual mode.
 *
 * @return Whether the ui needs to be updated.
 */
int visual_handle_input(int c);

#endif
