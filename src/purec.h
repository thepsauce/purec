#ifndef PUREC_H
#define PUREC_H

/********************************************************
 * PPPPPPP    U      U   RRRRRRR    EEEEEEEE     CCCCC  *
 *  P     P   U      U    R     R    E     E    C     C *
 *  P     P   U      U    R     R    E         C        *
 *  PPPPPP    U      U    RRRRRR     E   E     C        *
 *  P         U      U    R  R       EEEEE     C        *
 *  P         U      U    R   R      E   E     C        *
 *  P         U      U    R    R     E         C        *
 *  P         U      U    R    R     E     E    C     C *
 * PPP         UUUUUU    RR    R    EEEEEEEE     CCCCC  *
 ********************************************************/

#include "line.h"
#include "util.h"

#include <ncurses.h>

/**
 * Off screen window that can be used for measuring.
 *
 * Its size is 512x1.
 */
extern WINDOW *OffScreen;

#define MSG_DEFAULT     0
#define MSG_TO_DEFAULT  1
#define MSG_OTHER       2

#define NORMAL_MODE 0 /* 0 */
#define INSERT_MODE 1 /* 1 */

#define IS_VISUAL(mode) (!!((mode)&2))
#define VISUAL_MODE 2 /* 2 */
#define VISUAL_LINE_MODE (2|1) /* 3 */
#define VISUAL_BLOCK_MODE (2|4) /* 6 */

/**
 * The core struct contains information about all modes and the state of the
 * editor.
 */
extern struct core {
    /**
     * Message that is rendered at the bottom of the screen.
     *
     * The size of the window is 128x1.
     */
    WINDOW *msg_win;
    /**
     * The state of the message.
     */
    int msg_state;

    /// whether the editor should quit
    bool is_stopped;
    /// the exit code to return in `main()`
    int exit_code;

    /// the previous column count
    int prev_cols;
    /// the previous line count
    int prev_lines;

    /// type of the mode (`*_MODE`)
    int mode;
    /// counter
    size_t counter;
    /// user register
    char user_reg;

    /**
     * saved registers, '.' is the default register, it is used when no other
     * register was given
     */
    struct reg {
        int flags;
        size_t data_i;
    } regs['Z' - '.'];

    /// saved cursor positon (for visual mode)
    struct pos pos;
    /// normal mode
    struct normal_mode {
        /* TODO: move jumps */
        struct pos *jumps;
        size_t num_jumps;
        size_t jump_i;
    } normal;

    /// event from which the last insert mode started
    size_t ev_from_ins;
    /// how many times to move down
    size_t move_down_count;

    /* All variables below here shall NOT be modified while a recording is
     * playing. To check if a recording is playing, do
     * `if (is_playback())`.
     */

    /**
     * Series of key presses. Keys larger than 0xff, for example `KEY_LEFT` are
     * encoded with (in binary) 1111111X XXXXXXXX, so this encodes the range from
     * 256 to 511 (0777 is the maximum ncurses key value) (inclusive).
     * This works because even in UTF-8 because 11111110 or 11111111 mean
     * nothing.
     */
    char *rec;
    /// the amount of inputted characters
    size_t rec_len;
    /// number of allocated bytes for `rec`
    size_t a_rec;
    /// how many times to play the recording from `dot_i`
    size_t repeat_count;
    /// the start of the dot recording segment
    size_t dot_i;
    /// the end of the dot recording segment
    size_t dot_e;
    /// the index within the current playback
    size_t play_i;
    /// user created recordings 'A' to 'Z'
    struct {
        size_t from;
        size_t to;
    } user_recs['Z' - 'A' + 1];
    /// current user recording (lower case), '\0' signals nothing being recorded
    char user_rec_ch;
} Core;

struct selection {
    /// if it is a block selection
    bool is_block;
    /// beginning of the selection
    struct pos beg;
    /// end of the selection
    struct pos end;
};

/**
 * Initializes the internal clipboard data.
 */
void init_clipboard(void);

/**
 * Copies given lines to the clipboard.
 *
 * @param lines     The lines to copy to the clipboard.
 * @param num_lines The number of lines.
 * @param primary   For X11, if the primary clipboard should be used.
 *
 * @return 0 if the clipboard could be accessed, -1 otherwise.
 */
int copy_clipboard(const struct raw_line *lines, size_t num_lines, int primary);

/**
 * Reads lines from the clipboard.
 *
 * @param p_num_lines   The number of lines read.
 * @param primary       For X11, if the primary clipboard should be used.
 *
 * @return The lines read, they shall NOT be freed or modified.
 */
struct raw_line *paste_clipboard(size_t *p_num_lines, int primary);

/**
 * Sets the string shown in the message window.
 *
 * @param msg   The format string (like `printf()`)
 * @param ...   The format string arguments
 */
void set_message(const char *msg, ...);

/**
 * Sets the string shown in the message window and applies the error highlight.
 *
 * @param err   The format string (like `printf()`)
 * @param ...   The format string arguments
 */
void set_error(const char *err, ...);

/**
 * Sets the current register to given data.
 *
 * @param data_i    The data index.
 * @param flags     The register flags.
 */
void yank_data(size_t data_i, int flags);

/**
 * Sets the current register to given lines.
 *
 * @param lines     The lines.
 * @param num_lines The numer of lines lines.
 * @param flags     The register flags.
 */
void yank_lines(struct raw_line *lines, size_t num_lines, int flags);

/**
 * Checks if a recording is currently being played. If yes, then the next call
 * to `get_ch()` will NOT be interactive user input but a playback.
 *
 * @return If a recording is played.
 */
bool is_playback(void);

/**
 * Get input from the user or the playback record.
 *
 * @return Next input character.
 */
int get_ch(void);

/**
 * Gets user input like normal but handle digits in a special way.
 *
 * This function uses `get_ch()`.
 *
 * If the input is '0' and the counter is 0, then '0' is returned but when the
 * counter is non zero, then the counter is multiplied by 10 and the next
 * character is read. For any other digit ('1' to '9') it is appended to the
 * counter and the next character is retrieved.
 *
 * @return Next input character.
 */
int get_extra_char(void);

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
 * If the current mode is not a visual mode but the new mode is, `Core.pos` is
 * set to the cursor position within the current frame (`SelFrame`). This is
 * used to render the selection and do actions upon it.
 *
 * @param mode  The new mode.
 */
void set_mode(int mode);

/**
 * Gets the selection of the current frame.
 *
 * This simply gets the cursor position within the current frame and `Core.pos`
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

#define UPDATE_UI 0x1
#define DO_RECORD 0x2

/**
 * Handles a key input for the normal mode.
 *
 * @return Bit wise OR of the above flags.
 */
int normal_handle_input(int c);

/**
 * Handles a key input for the insert mode.
 *
 * @return Bit wise OR of the above flags.
 */
int insert_handle_input(int c);

/**
 * Handles a key input for the visual mode, this also includes the visual line
 * mode and visual block mode.
 *
 * @return Bit wise OR of the above flags.
 */
int visual_handle_input(int c);

#endif
