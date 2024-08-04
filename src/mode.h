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

#define NORMAL_MODE 0 /* 0 */
#define INSERT_MODE 1 /* 1 */

#define IS_VISUAL(mode) (!!((mode)&2))
#define VISUAL_MODE 2 /* 2 */
#define VISUAL_LINE_MODE (2|1) /* 3 */
#define VISUAL_BLOCK_MODE (2|4) /* 6 */

/*
extern struct bind {
    const char *mode;
    char b[4];
    const char *desc;
} Binds[] = {
    { "i", "\x1b", "enter normal mode" }, #

    { "n", "/", "search for pattern" }, ?
    { "n", "?", "search for pattern backwards" }, ?

    { "n", "A", "enter insert mode and go to the end of the line" }, #
    { "n", "B", "alias: b" }, ?
    { "n", "C", "delete to the end of the line and enter insert mode" }, ?
    { "n", "D", "delete to the end of the line" }, ?
    { "n", "E", "alias: e" }, ?
    { "n", "F", "jump to previous character" }, ?
    { "n", "G", "go to the end of the file" }, #
    { "n", "H", "go to the start of the frame" }, ?
    { "n", "I", "goto to the start of the line and enter insert mode" }, #
    { "n", "J", "join current line with next lines" }, ?
    { "n", "K", "search for identifier in the manual" }, ?
    { "n", "L", "go to the end of the frame" }, ?
    { "n", "M", "go to the middle of the frame" }, ?
    { "n", "N", "go to the previous match" }, ?
    { "n", "O", "insert line above the cursor and enter insert mode" }, #
    { "n", "P", "paste yanked text before the cursor" }, ?
    { "n", "Q", NULL },
    { "n", "R", "enter replace mode" }, ?
    { "n", "S", "alias: cc" }, #
    { "n", "T", "jump after previous character" }, ?
    { "n", "U", "undo current line" }, ?
    { "n", "V", "enter visual block mode" }, #
    { "n", "W", "alias: w" }, ?
    { "n", "X", "delete character before the cursor" }, #
    { "n", "Y", "alias: yy" }, ?
    { "n", "Z", "open file opener" }, ?

    { "n", "a", "switch to append mode" }, #
    { "n", "b", "jump to beginning of word" }, ?
    { "n", "c", NULL },
    { "n", "cc", "delete line, indent line and go to insert mode" } #
    { "n", "d", NULL },
    { "n", "dd", "delete line" }, #
    { "n", "e", "jump to the end of the word" }, ?
    { "n", "f", "jump to given character" }, ?
    { "n", "g", NULL },
    { "n", "gG", "go to end of the file" }, #
    { "n", "gg", "go to the beginning of the file" }, #
    { "n", "h", "go to the left" }, #
    { "n", "i", "enter insert mode" }, #
    { "n", "j", "go down" }, #
    { "n", "k", "go up" }, #
    { "n", "l", "go to the right" }, #
    { "n", "m", "set a mark" }, ?
    { "n", "n", "go to next match" }, ?
    { "n", "o", "add line below the cursor and enter insert mode" }, #
    { "n", "p", "paste text" }, ?
    { "n", "q", "record macro" }, ?
    { "n", "r", "replace character" }, ?
    { "n", "s", "delete character and enter insert mode" }, #?
    { "n", "t", "jump right before given character" }, ?
    { "n", "u", "undo" }, #
    { "n", "v", "enter visual mode" }, #
    { "n", "w", "jump over a word" }, ?
    { "n", "x", "delete character" }, #
    { "n", "y", NULL },
    { "n", "yy", "yank line" }, ?
    { "n", "z", NULL },
};*/

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
 * Get user input like normal but handle digits in a special way.
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
 * Get the selection of the current frame.
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
