#ifndef FRAME_H
#define FRAME_H

/************************
 *         Frame        *
 ************************/

#include "buf.h"

/**
 * A frame is a rectangle on the screen that shows a buffer.
 *
 * It allows a cursor to move through the buffer and make changes.
 *
 * Note that the frame does not implement any buffer editing functions itself,
 * it merely shows the buffer and allows to move through it. `buf.h/c` implement
 * all edit operations.
 */
struct frame {
    /// position and size on the screen
    int x, y, w, h;
    /// the most recent direction the frame did a split
    int split_dir;
    /// buffer that is active within this frame
    struct buf *buf;
    /// cursor position within the buffer
    struct pos cur;
    /**
     * the next value the cursor position should take one,
     * the column might be out of bounds
     */
    struct pos next_cur;
    /// cursor position before a big jump
    struct pos prev_cur;
    /// offset of the text origin
    struct pos scroll;
    /// vertical column tracking
    size_t vct;
    /// the next value for `vct`
    size_t next_vct;
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
 * Gets the cursor position relative to the screen origin.
 *
 * @param frame The frame to get the cursor from.
 * @param p_x   The resulting x position.
 * @param p_y   The resulting y position.
 *
 * @return Whether the cursor is visible.
 */
bool get_visual_cursor(const struct frame *frame, int *p_x, int *p_y);

/**
 * Gets the number of frames visible by iterating the frame linked list.
 *
 * @return The number of visible frames.
 */
size_t get_frame_count(void);

/**
 * Gets the frame at given position.
 *
 * @param x The x position to check.
 * @param y The y position to check.
 *
 * @return The frame at given position.
 */
struct frame *get_frame_at(int x, int y);

/**
 * Destroys all frames but the given one.
 *
 * @param frame The frame to keep.
 */
void set_only_frame(struct frame *frame);

/**
 * Resizes all frames according to the new screen size in `COLS`, `LINES`.
 */
void update_screen_size(void);

/**
 * Moves the left edge of a frame to the left.
 *
 * @param frame     The frame whose edge to move left.
 * @param amount    The amount to move it left by.
 *
 * @return The amount it actually moved by.
 */
int move_left_edge(struct frame *frame, int amount);

/**
 * Moves the right edge of a frame to the right.
 *
 * @param frame     The frame whose edge to move right.
 * @param amount    The amount to move it right by.
 *
 * @return The amount it actually moved by.
 */
int move_right_edge(struct frame *frame, int amount);

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
 * Computes the position the given motion implies.
 *
 * Note: If this function returns 0, then `apply_motion()` cannot be called.
 *
 * @param frame         Frame to perform the motion within.
 * @param motion_key    The input key.
 *
 * @return 0 if the cursor would not move, 1 otherwise.
 */
int prepare_motion(struct frame *frame, int motion_key);

/**
 * Sets the cursor to the new position computed by `prepare_motion()`.
 *
 * Note: This function should be called after a call to `prepare_motion()`.
 *
 * @param frame The frame to apply the motion to.
 *
 * @return This function always returns 1.
 */
int apply_motion(struct frame *frame);

#define do_motion(frame, key) (prepare_motion(frame, (key)) && \
                               apply_motion(frame))

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
