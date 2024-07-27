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
 */
struct frame {
    /// position and size on the screen
    int x, y, w, h;
    /// buffer that is active within this frame
    struct buf *buf;
    /// cursor position
    struct pos cur;
    /// vertical column tracking
    size_t vct;
};

/**
 * Currently selected frame.
 */
extern struct frame *SelFrame;

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
 * This places the cursor at `vct` and places it in bound vertically.
 *
 * If the cursor is out of bounds vertically, it is set to the last line. Then
 * the vertical column tracker is used to adjust the cursor horizontally.
 *
 * @param frame Frame to adjust the cursor within.
 */
void adjust_cursor(struct frame *frame);

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

#endif
