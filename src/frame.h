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
 * This adjust the cursor to an in bound position.
 *
 * If the cursor is out of bounds vertically, it is set to the last line, then
 * the vertical column tracker is used to adjust the cursor horizontally.
 *
 * @param frame Frame to adjust the cursor within.
 */
void adjust_cursor(struct frame *frame);

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
