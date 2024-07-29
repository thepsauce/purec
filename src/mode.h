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

/**
 * Sets `p1` to the position that comes first and `p2` to last.
 *
 * @param p1    The first position.
 * @param p2    The second position.
 */
void sort_positions(struct pos *p1, struct pos *p2);

#define NORMAL_MODE 0
#define INSERT_MODE 1
#define VISUAL_MODE 2

#define EXTRA_NOTHING 0
#define EXTRA_DELETE 1
#define EXTRA_CHANGE 2

/*
extern struct bind {
    const char *mode;
    char b[4];
    const char *desc;
} Binds[] = {
    { "i", "\x1b", "enter normal mode" },

    { "n", "/", "search for pattern" },
    { "n", "?", "search for pattern backwards" },

    { "n", "A", "enter insert mode and go to the end of the line" },
    { "n", "B", "alias: b" },
    { "n", "C", "delete to the end of the line and enter insert mode" },
    { "n", "D", "delete to the end of the line" },
    { "n", "E", "alias: e" },
    { "n", "F", "jump to previous character" },
    { "n", "G", "go to the end of the file" },
    { "n", "H", "go to the start of the frame" },
    { "n", "I", "goto to the start of the line and enter insert mode" },
    { "n", "J", "join current line with next lines" },
    { "n", "K", "search for identifier in the manual" },
    { "n", "L", "go to the end of the frame" },
    { "n", "M", "go to the middle of the frame" },
    { "n", "N", "go to the previous match" },
    { "n", "O", "insert line above the cursor and enter insert mode" },
    { "n", "P", "paste yanked text before the cursor" },
    { "n", "Q", NULL },
    { "n", "R", "enter replace mode" },
    { "n", "S", "alias: cc" },
    { "n", "T", "jump after previous character" },
    { "n", "U", "undo current line" },
    { "n", "V", "enter visual block mode" },
    { "n", "W", "alias: w" },
    { "n", "X", "delete character before the cursor" },
    { "n", "Y", "alias: yy" },
    { "n", "Z", "open file opener" },

    { "n", "a", "switch to append mode" },
    { "n", "b", "jump to beginning of word" },
    { "n", "c", NULL },
    { "n", "cc", "delete line, indent line and go to insert mode" }
    { "n", "d", NULL },
    { "n", "dd", "delete line" },
    { "n", "e", "jump to the end of the word" },
    { "n", "f", "jump to given character" },
    { "n", "g", NULL },
    { "n", "gG", "go to end of the file" },
    { "n", "gg", "go to the beginning of the file" },
    { "n", "h", "go to the left" },
    { "n", "i", "enter insert mode" },
    { "n", "j", "go down" },
    { "n", "k", "go up" },
    { "n", "l", "go to the right" },
    { "n", "m", "set a mark" },
    { "n", "n", "go to next match" },
    { "n", "o", "add line below the cursor and enter insert mode" },
    { "n", "p", "paste text" },
    { "n", "q", "record macro" },
    { "n", "r", "replace character" },
    { "n", "s", "delete character and enter insert mode" },
    { "n", "t", "jump right before given character" },
    { "n", "u", "undo" },
    { "n", "v", "enter visual mode" },
    { "n", "w", "jump over a word" },
    { "n", "x", "delete character" },
    { "n", "y", NULL },
    { "n", "yy", "yank line" },
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
 * @param mode  The new mode.
 */
void set_mode(int mode);

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

#endif
