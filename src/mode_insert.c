#include "frame.h"
#include "buf.h"
#include "mode.h"
#include "util.h"

#include <ctype.h>
#include <ncurses.h>

int insert_handle_input(int c)
{
    static int motions[KEY_MAX] = {
        [KEY_LEFT] = MOTION_LEFT,
        [KEY_RIGHT] = MOTION_RIGHT,
        [KEY_UP] = MOTION_UP,
        [KEY_DOWN] = MOTION_DOWN,

        [KEY_HOME] = MOTION_HOME,
        [KEY_END] = MOTION_END,

        [KEY_PPAGE] = MOTION_PAGE_UP,
        [KEY_NPAGE] = MOTION_PAGE_DOWN,
    };

    int r = 0;
    char ch[1];
    size_t n;
    struct pos old_cur, new_cur;

    switch (c) {
    case '\x1b':
        move_horz(SelFrame, 1, -1);
        set_mode(NORMAL_MODE);
        r = 1;
        break;

    case '\n':
        ch[0] = '\n';
        insert_text(SelFrame->buf, &SelFrame->cur, ch, 1);
        do_motion(SelFrame, MOTION_NEXT);
        return 1;

    case '\t':
        ch[0] = ' ';
        n = TABSIZE - SelFrame->cur.col % TABSIZE;
        for (size_t i = 0; i < n; i++) {
            insert_text(SelFrame->buf, &SelFrame->cur, ch, 1);
        }
        Mode.counter = n;
        do_motion(SelFrame, MOTION_NEXT);
        return 1;

    case KEY_DC:
        old_cur = SelFrame->cur;
        r = move_dir(SelFrame, 1, 1);
        ev = delete_range(buf, &old_cur, &SelFrame->cur);
        if (ev != NULL) {
            ev->cur = old_cur;
        }
        SelFrame->cur = old_cur;
        return r;

    case 0x7f:
    case KEY_BACKSPACE:
        old_cur = SelFrame->cur;
        r = move_dir(SelFrame, 1, -1);
        ev = delete_range(buf, &old_cur, &SelFrame->cur);
        if (ev != NULL) {
            ev->cur = old_cur;
        }
        return r;
    }
    ch[0] = c;
    if (c < 0x100 && ch[0] >= ' ' && motions[c] == 0) {
        insert_text(SelFrame->buf, &SelFrame->cur, ch, 1);
        do_motion(SelFrame, MOTION_NEXT);
        return 1;
    }
    return do_motion(SelFrame, motions[c]);
}
