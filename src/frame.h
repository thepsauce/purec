#ifndef FRAME_H
#define FRAME_H

#include "buf.h"

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

int handle_input(struct frame *frame, int c);

#endif
