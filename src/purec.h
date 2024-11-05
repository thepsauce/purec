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

#include "text.h"
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

struct indent_rule {
    /// the number of spaces of a tab
    int tab_size;
    /// whether to expand \t to spaces
    bool use_spaces;
};

#define REG_MIN '.'
#define REG_MAX 'Z'

#define IS_REG_CHAR(r) (((r)>=REG_MIN&&(r)<=REG_MAX)||(r)=='+'||(r)=='*')

/* note that there are special mark outside this range, see `get_mark()` */
#define MARK_MIN 'A'
#define MARK_MAX 'Z'

#define USER_REC_MIN 'A'
#define USER_REC_MAX 'Z'

struct fixit {
    struct buf *buf;
    struct pos pos;
    char msg[128];
};

/**
 * The core struct contains information about all modes and the state of the
 * editor.
 */
extern struct core {
    /// default indentation to use for buffers
    struct indent_rule rule;

    /// the currently selected theme
    int theme;

    /**
     * Message that is rendered at the bottom of the screen.
     *
     * The size of the window is 128x1.
     */
    WINDOW *msg_win;
    /// the state of the message
    int msg_state;

    /// contains the current characters pressed
    WINDOW *preview_win;

    /// whether the editor should quit
    bool is_stopped;
    /// the exit code to return in `main()`
    int exit_code;

    /// cache directory
    char *cache_dir;
    /// directory in which sessions are stored in
    char *session_dir;

    /// what direction the last search went in
    int search_dir;

    /// type of the mode (`*_MODE`)
    int mode;
    /// current counter (number before a command)
    size_t counter;
    /// selected user register
    char user_reg;

    /**
     * saved registers, '.' is the default register, it is used when no other
     * register was given
     */
    struct reg {
        int flags;
        struct undo_seg *seg;
    } regs[REG_MAX - REG_MIN + 1];

    /// saved cursor positon (for visual mode)
    struct pos pos;

    /// event from which the last insert mode started
    size_t ev_from_insert;
    /// how many times to repeat the insertion
    size_t repeat_insert;
    /// how many times to move down and repeat the insertion
    line_t move_down_count;

    /// saved positions within a buffer
    struct mark {
        struct buf *buf;
        struct pos pos;
    } marks[MARK_MAX - MARK_MIN + 1], last_insert;

    /* All variables below here shall NOT be modified while a recording is
     * playing. To check if a recording is playing, do
     * `if (is_playback())`.
     */

    /**
     * Series of key presses. Keys larger than 0xff, for example `KEY_LEFT` are
     * encoded with (in binary) 11111111 XXXXXXXX, so this encodes the range from
     * 256 to 511 (0777 is the maximum ncurses key value) (inclusive).
     * This works because even in UTF-8 11111111 means nothing.
     */
    char *rec;
    /// the amount of inputted characters
    size_t rec_len;
    /// number of allocated bytes for `rec`
    size_t a_rec;
    /// the current recording stack
    struct play_rec {
        /// start of the playback; index within `rec`
        size_t from;
        /// end of the playback (exclusive); index within `rec`
        size_t to;
        /// current index of the playback; index within `rec`
        size_t index;
        /// how many times to replay before popping
        size_t repeat_count;
    } rec_stack[32];
    /// the number of recordings on the stack
    unsigned rec_stack_n;
    /// user created recordings 'A' to 'Z'
    struct rec {
        /// start of the recording; index within `rec`
        size_t from;
        /// end of the recording (exclusive); index within `rec`
        size_t to;
    } user_recs[USER_REC_MAX - USER_REC_MIN + 1];
    /// current user recording (upper case), '\0' signals nothing being recorded
    char user_rec_ch;
    /// the dot recording
    struct rec dot;

    /// all points where a buffer requires a fix
    struct fixit *fixits;
    /// the number of fixes required
    size_t num_fixits;
    /// the current fixit item
    size_t cur_fixit;

    /// if the core is not waiting for input
    bool is_busy;
    /// the number of sigints sent
    size_t num_sigs;
    /// the current running child process
    pid_t child_pid;
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
 * Copies given data segment into the clipboard.
 *
 * @param seg       The data segment to copy.
 * @param primary   For X11, if the primary clipboard should be used.
 *
 * @return 0 if the clipboard could be accessed, -1 otherwise.
 */
int copy_clipboard(struct undo_seg *seg, int primary);

/**
 * Reads lines from the clipboard.
 *
 * @param p_num_lines   The number of lines read.
 * @param primary       For X11, if the primary clipboard should be used.
 *
 * @return The lines read, they shall NOT be freed or modified.
 */
bool paste_clipboard(struct text *text, int primary);

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

/* forward declare the frame struct */
struct frame;

/**
 * Gets the mark associated to given character.
 *
 * @param frame The frame to consider for special marks.
 * @param ch    The identifier of the mark.
 *
 * @return A pointer to static memory containing the mark or `NULL`.
 */
struct mark *get_mark(struct frame *frame, char ch);

/**
 * Sets the current register to given data.
 *
 * @param seg   The data segment.
 * @param flags The register flags.
 */
void yank_data(struct undo_seg *seg, int flags);

/**
 * Checks if a recording is currently being played.
 *
 * If yes, then the next call to `get_ch()` will NOT be interactive user input
 * but a playback.
 *
 * @return The currently playing recording.
 */
struct play_rec *get_playback(void);

/**
 * Get input from the user or the playback record.
 *
 * @return Next input character or -1 on a resize event.
 */
int get_ch(void);

/**
 * Get input from the user or the playback record.
 *
 * The difference to `get_ch()` is that -1 is never returned.
 *
 * @return The next input character.
 */
int get_char(void);

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
col_t get_mode_line_end(const struct line *line);

/**
 * Gets the frame that has the given buffer selected.
 *
 * @param buf   The buffer to look for.
 *
 * @return The frame containing the buffer.
 */
struct frame *get_frame_with_buffer(const struct buf *buf);

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

/**
 * Selects the buffer that has the next/prev fix it item and moves the cursor
 * to it.
 *
 * @param dir   The direction to go in.
 * @param count The number of items to jump over.
 */
void goto_fixit(int dir, size_t count);

#define UPDATE_UI       0x1
#define DO_NOT_RECORD   0x2

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

/**
 * Frees all resources in a way where another session can be loaded in.
 */
void free_session(void);

/**
 * Saves the current session into given file.
 *
 * @param fp    The file to write the session to.
 */
void save_session(FILE *fp);

/**
 * Loads a session from a file.
 *
 * This function assumes that nothing is loaded yet and that `FirstBuffer` and
 * `FirstFrame` are both `NULL`.
 *
 * @param fp    The file to load the session from.
 *
 * @return -1 if the file had any mistakes, 0 otherwise.
 */
int load_session(FILE *fp);

/**
 * Saves the current session and returns the file name.
 *
 * @return The name of the file the session was written to.
 */
char *save_current_session(void);

/**
 * Opens a fuzzy dialog and lets the user choose a session.
 */
void choose_session(void);

/**
 * Runs given command, `cmd` might get modified.
 *
 * @param cmd   The command line to run.
 *
 * @return -1 if the command was successfully run, 0 otherwise.
 */
int run_command(char *cmd);

/**
 * Reads a command line and executes given command.
 *
 * `beg` determines what type of command line it is. These are all options:
 * "/"  Search
 * "?"  Search backwards
 * ":"  Run command
 *
 * @param beg   What the command line should start with, must be larger than 1.
 */
void read_command_line(const char *beg);

/**
 * Jumps to the file within the buffer at given position.
 *
 * @param buf   The buffer to check for a file name.
 * @param pos   The position of the cursor.
 */
void jump_to_file(const struct buf *buf, const struct pos *pos);

/**
 * Sends the character to the current mode handler.
 *
 * @param c     The character to send.
 *
 * @return Whether the ui needs re-rendering.
 */
int handle_input(int c);

/**
 * Initialize the core.
 *
 * @param argc  The number of program arguments.
 * @param argv  The program arguments.
 *
 * @return 0 if initializing was successful, -1 otherwise.
 */
int init_purec(int argc, char **argv);

/**
 * Renders all frames and places the cursor
 */
void render_all(void);

/**
 * Frees all resources, saves the last session.
 *
 * @return The exit code of purec.
 */
int leave_purec(void);

#endif
