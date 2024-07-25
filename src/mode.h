#ifndef MODE_H
#define MODE_H

/* ** *** **** *** ** *
 *        Mode        *
 * ** *** **** *** ** */

#include "line.h"

/**
 * Position within "something".
 */
struct pos {
    size_t line;
    size_t col;
};

#define NORMAL_MODE 0
#define INSERT_MODE 1
#define VISUAL_MODE 2

#define EXTRA_NOTHING 0
#define EXTRA_DELETE 1
#define EXTRA_CHANGE 2

/**
 * The mode struct contains information about all modes.
 */
extern struct mode {
    /// type of the mode (`*_MODE`)
    int type;
    /// addional mode states (`EXTRA_*`)
    int extra;
    /// additional counter
    size_t extra_counter;
    /// counter
    size_t counter;
    /// saved cursor positon
    struct pos pos;
    /// normal mode
    struct normal_mode {
        /* TODO: move jumps */
        struct pos *jumps;
        size_t num_jumps;
        size_t jump_i;
    } normal;
} Mode;

/**
 * Multiplies the counter by 10 and adds given number to it.
 *
 * When it would overflow, the counter is set to `SIZE_MAX` instead.
 *
 * @param d Number to add (best between 0 and 9 (inclusive))
 */
void shift_add_counter(int d);

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
 * Sets the new mode and does any transition needed like changing cursor shape.
 *
 * @param mode  The new mode.
 */
void set_mode(int mode);

/**
 * Handles a key input for the normal mode.
 *
 * @return Whether the ui needs to be updated.
 */
int normal_handle_input(int c);

#endif
