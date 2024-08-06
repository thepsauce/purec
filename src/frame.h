#ifndef FRAME_H
#define FRAME_H

/**********************
 *        Frame       *
 **********************/

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
    /// next frame in the focus linked list
    struct frame *next_focus;
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
 * Create a frame and split if off given frame.
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
 * Gets the frame at given position.
 *
 * @param x The x position to check.
 * @param y The y position to check.
 *
 * @return The frame at given position.
 */
struct frame *frame_at(int x, int y);

/**
 * Focuses given frame.
 *
 * @return The last focused frame.
 */
struct frame *focus_frame(struct frame *frame);

/**
 * Render the frame within its defined bounds.
 *
 * @param frame The frame to render.
 */
void render_frame(struct frame *frame);

/**
 * Adjusts `scroll` such that the cursor is visible.
 *
 * @param frame Frame to adjust the `scroll` within.
 *
 * @return Whether scrolling occured.
 */
int adjust_scroll(struct frame *frame);

/**
 * This places the cursor at `vct` and places it in bound vertically.
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
#define MOTION_LEFT         1
/// move to the right
#define MOTION_RIGHT        2
/// move up
#define MOTION_UP           3
/// move down
#define MOTION_DOWN         4
/// move to the beginning of the line
#define MOTION_HOME         5
/// move to the end of the line
#define MOTION_END          6
/// move to the next character
#define MOTION_NEXT         7
/// move to the previous character
#define MOTION_PREV         8
/// move to the beginning of the line but skip white space
#define MOTION_HOME_SP      9
/// move to the beginning of the file
#define MOTION_FILE_BEG     10
/// move to the end of the file
#define MOTION_FILE_END     11
/// move a page up
#define MOTION_PAGE_UP      12
/// move a page down
#define MOTION_PAGE_DOWN    13
/// move up a paragraph
#define MOTION_PARA_UP      14
/// move down a paragraph
#define MOTION_PARA_DOWN    15

/**
 * Do a special cursor motion.
 *
 * @param frame     Frame to perform the motion within.
 * @param motion    Motion to perform (`MOTION_*`).
 *
 * @return 0 if the cursor stayed unchanged, 1 otherwise
 */
int do_motion(struct frame *frame, int motion);

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
