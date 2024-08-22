#ifndef FRAME_H
#define FRAME_H

/************************
 *         Frame        *
 ************************/

#include "buf.h"

/**
 * A frame is a rectangle on the screen that shows a buffer.
 *
 * It allows a cursor to move over the buffer and make changes.
 *
 * Note that the frame does not implement any buffer editing functions itself,
 * it merely shows the buffer and allows to move through it. `buf.h/c` implement
 * all edit operations.
 */
struct frame {
    /// position and size on the screen
    int x, y, w, h;
    /// buffer that is active within this frame
    struct buf *buf;
    /// cursor position
    struct pos cur;
    /// scrolling (there is no horizontal scrolling)
    struct pos scroll;
    /// vertical column tracking
    size_t vct;
    /// next frame in the linked list
    struct frame *next;
};

/**
 * The first frame in the linked list of frames.
 */
extern struct frame *FirstFrame;

/**
 * Currently selected frame.
 */
extern struct frame *SelFrame;

#define SPLIT_NONE  0
#define SPLIT_LEFT  1
#define SPLIT_RIGHT 2
#define SPLIT_UP    3
#define SPLIT_DOWN  4

/**
 * Creates a frame and split if off given frame.
 *
 * @param split The frame to split off.
 * @param dir   The direction of splitting.
 * @param buf   The buffer of the new frame.
 *
 * @return The newly created frame.
 */
struct frame *create_frame(struct frame *split, int dir, struct buf *buf);

/**
 * Destroys given frame and unsplits it.
 *
 * This function sets `IsRunning` to false if all frames are gone.
 *
 * @param frame The frame to destroy.
 */
void destroy_frame(struct frame *frame);

/**
 * Renders the frame within its defined bounds.
 *
 * @param frame The frame to render.
 */
void render_frame(struct frame *frame);

/**
 * Get the text rect relative to the frame origion.
 *
 * This includes the status bar, vertical separator and line numbers.
 *
 * @param frame The frame to get the text region from.
 * @param p_x   The result of the x coordinate.
 * @param p_y   The result of the y coordinate.
 * @param p_w   The result of the width.
 * @param p_h   The result of the height.
 */
void get_text_rect(const struct frame *frame,
        int *p_x, int *p_y, int *p_w, int *p_h);

/**
 * Gets the visible relative cursor position of the frame.
 *
 * This cursor is independent of the scrolling values;
 *
 * @param frame The frame to get the cursor from.
 * @param pos   The resulting position.
 */
void get_visual_cursor(const struct frame *frame, struct pos *pos);

/**
 * Gets the frame at given position.
 *
 * @param x The x position to check.
 * @param y The y position to check.
 *
 * @return The frame at given position.
 */
struct frame *frame_at(int x, int y);

/**
 * Resizes all frames according to the new screen size in `COLS`, `LINES`.
 */
void update_screen_size(void);

/**
 * Changes the buffer of a frame.
 *
 * @param frame The frame of which the buffer should be changed.
 * @param buf   The new buffer of the frame.
 */
void set_frame_buffer(struct frame *frame, struct buf *buf);

/**
 * Adjusts `scroll` such that the cursor is visible.
 *
 * @param frame The frame whose `scroll` to adjust.
 *
 * @return Whether scrolling occured.
 */
int adjust_scroll(struct frame *frame);

/**
 * Scrolls the frame by a distance in a direction.
 *
 * @param frame Frame to scroll.
 * @param dist  Distance to scroll.
 * @param dir   The direction to go.
 */
int scroll_frame(struct frame *frame, size_t dist, int dir);

/**
 * Places the cursor at `vct` and places it in bound vertically.
 *
 * If the cursor is out of bounds vertically, it is set to the last line. Then
 * the vertical column tracker is used to adjust the cursor horizontally.
 *
 * @param frame Frame to adjust the cursor within.
 */
void adjust_cursor(struct frame *frame);

/**
 * Clips the cursor to the line end (mode aware).
 *
 * @param frame The frame whose cursor to use.
 */
void clip_column(struct frame *frame);

/**
 * Sets the cursor to a new position.
 *
 * This also clips vertically and horizontally, the `vct` is NOT used.
 *
 * @param frame Frame to set the cursor in.
 * @param pos   The new cursor position.
 */
void set_cursor(struct frame *frame, const struct pos *pos);

/**
 * All motions use the counter to perform the operation count times.
 */

/// move to the left
#define MOTION_LEFT             1
/// move to the right
#define MOTION_RIGHT            2
/// move up
#define MOTION_UP               3
/// move down
#define MOTION_DOWN             4
/// move to the beginning of the line
#define MOTION_HOME             5
/// move to the end of the line
#define MOTION_END              6
/// move to the next character
#define MOTION_NEXT             7
/// move to the previous character
#define MOTION_PREV             8
/// move to the start of the frame
#define MOTION_BEG_FRAME        9
/// move to he middle of the frame
#define MOTION_MIDDLE_FRAME     10
/// move to he end of the frame
#define MOTION_END_FRAME        11
/// move to the beginning of the line but skip white space
#define MOTION_HOME_SP          12
/// move to the beginning of the file
#define MOTION_FILE_BEG         13
/// move to the end of the file
#define MOTION_FILE_END         14
/// move a page up
#define MOTION_PAGE_UP          15
/// move a page down
#define MOTION_PAGE_DOWN        16
/// move up a paragraph
#define MOTION_PARA_UP          17
/// move down a paragraph
#define MOTION_PARA_DOWN        18
/// finds a next character, it uses `get_ch()`
#define MOTION_FIND_NEXT        19
/// finds a previous character, it uses `get_ch()`
#define MOTION_FIND_PREV        20
/// same as `MOTION_FIND_NEXT` but jump before match
#define MOTION_FIND_EXCL_NEXT   21
/// same as `MOTION_FIND_PREV` but jump after match
#define MOTION_FIND_EXCL_PREV   22
/// move to next word
#define MOTION_NEXT_WORD        23
/// move to the end of the next word
#define MOTION_END_WORD         24
/// move to previous word
#define MOTION_PREV_WORD        25

/**
 * Does a special cursor motion.
 *
 * @param frame     Frame to perform the motion within.
 * @param motion    Motion to perform (`MOTION_*`).
 *
 * @return 0 if the cursor stayed unchanged, 1 otherwise
 */
int do_motion(struct frame *frame, int motion);

/**
 * Gets a motion from a keybind.
 *
 * Note that this function may ask the user for more input.
 *
 * @param c The input character.
 *
 * @return The corresponding motion or 0 if nothing bound to that key.
 */
int get_binded_motion(int c);

/**
 * Moves the cursor in horizontal direct but also vertically across lines.
 *
 * @param frame Frame to move the cursor within.
 * @param dist  Distance to move.
 * @param dir   Direction of the movement (negative or positive).
 *
 * @return 1 if a movement occured, 0 otherwise.
 */
int move_dir(struct frame *frame, size_t dist, int dir);

/**
 * Moves the cursor in vertical direction (up and down) and returns true if
 * there was movement.
 *
 * @param frame Frame to move the cursor within.
 * @param dist  Distance to move.
 * @param dir   Direction of the movement (negative or positive).
 *
 * @return 1 if a movement occured, 0 otherwise.
 */
int move_vert(struct frame *frame, size_t dist, int dir);

/**
 * Sets the line of the cursor to given line (clipped).
 *
 * @param frame Frame to move the cursor in.
 * @param line  Line to move to.
 *
 * @return Whether movement occured.
 */
int set_vert(struct frame *frame, size_t line);

/**
 * Moves the cursor in horizontal direction (left and right) and returns true if
 * there was movement.
 *
 * @param frame Frame to move the cursor within.
 * @param dist  Distance to move.
 * @param dir   Direction of the movement (negative or positive).
 *
 * @return 1 if a movement occured, 0 otherwise.
 */
int move_horz(struct frame *frame, size_t dist, int dir);

/**
 * Sets the column of the cursor to given column (clipped).
 *
 * @param frame Frame to move the cursor in.
 * @param col   Column to move to.
 *
 * @return Whether movement occured.
 */
int set_horz(struct frame *frame, size_t col);

#endif
