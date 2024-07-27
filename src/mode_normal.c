#include "frame.h"
#include "buf.h"
#include "mode.h"
#include "macros.h"

#include <ctype.h>
#include <ncurses.h>

size_t safe_mul(size_t a, size_t b)
{
    size_t c;

    if (__builtin_mul_overflow(a, b, &c)) {
        return SIZE_MAX;
    }
    return c;
}

int normal_handle_input(int c)
{
    static int motions[KEY_MAX] = {
        ['h'] = MOTION_LEFT,
        ['l'] = MOTION_RIGHT,
        ['k'] = MOTION_UP,
        ['j'] = MOTION_DOWN,

        ['0'] = MOTION_HOME,
        ['$'] = MOTION_END,

        [KEY_HOME] = MOTION_HOME,
        [KEY_END] = MOTION_END,

        ['I'] = MOTION_HOME_SP,
        ['A'] = MOTION_END,

        ['a'] = MOTION_RIGHT,

        ['g'] = MOTION_FILE_BEG,
        ['G'] = MOTION_FILE_END,

        [0x7f] = MOTION_PREV,
        [KEY_BACKSPACE] = MOTION_PREV,
        [' '] = MOTION_PREV,

        [KEY_PPAGE] = MOTION_PAGE_UP,
        [KEY_NPAGE] = MOTION_PAGE_DOWN,
    };

    int r = 0;
    struct pos old_cur, new_cur;
    int e_c;
    struct undo_event *ev;

    switch (c) {
    case '\x1b':
        move_horz(SelFrame, 1, -1);
        set_mode(NORMAL_MODE);
        r = 1;
        break;

    case 'g':
        Mode.extra_counter = Mode.counter;
        Mode.counter = 0;
        e_c = getch_digit();
        switch (e_c) {
        case 'g':
        case 'G':
            c = e_c;
            break;
        default:
            e_c = 0;
        }
        break;

    /* change or delete */
    case 'c':
    case 'd':
        Mode.extra_counter = Mode.counter;
        Mode.counter = 0;
        e_c = getch_digit();
        old_cur = SelFrame->cur;
        Mode.counter = safe_mul(correct_counter(Mode.counter),
                correct_counter(Mode.extra_counter));
        switch (e_c) {
        case 'c':
            if (c != 'c') {
                break;
            }
            r = delete_lines(SelFrame->buf, &SelFrame->cur, Mode.counter - 1);
            /* TODO: re indent line */
            if (SelFrame->buf->lines[SelFrame->cur.line].n != 0) {
                SelFrame->buf->lines[SelFrame->cur.line].n = 0;
                r = 1;
            }
            adjust_cursor(SelFrame);
            break;

        case 'd':
            if (c != 'd') {
                break;
            }
            r = delete_lines(SelFrame->buf, SelFrame->cur.line, Mode.counter);
            adjust_cursor(SelFrame);
            break;

        default:
            do_motion(SelFrame, motions[e_c]);
            r = delete_range(SelFrame->buf, &old_cur, &SelFrame->cur);
            if (old_cur.line < SelFrame->cur.line ||
                    (old_cur.line == SelFrame->cur.line &&
                     old_cur.col < SelFrame->cur.col)) {
                SelFrame->cur = old_cur;
            }
        }
        if (c == 'c') {
            set_mode(INSERT_MODE);
        }
        c = 0;
        break;

    case 'x':
        new_cur = SelFrame->cur;
        new_cur.col += correct_counter(Mode.counter);
        r = delete_range(SelFrame->buf, &SelFrame->cur, &new_cur);
        break;

    case 'X':
        old_cur = SelFrame->cur;
        r = move_horz(SelFrame, correct_counter(Mode.counter), -1);
        delete_range(SelFrame->buf, &old_cur, &SelFrame->cur);
        break;

    case 'u':
        ev = undo_event(SelFrame->buf);
        if (ev == NULL) {
            return 0;
        }
        SelFrame->cur = ev->cur;
        /* since the event could have been made in insert mode,
         * we need to clip it. just like below in redo
         */
        SelFrame->cur.col = MIN(SelFrame->cur.col, get_mode_line_end(
                    &SelFrame->buf->lines[SelFrame->cur.line]));
        return 1;

    case CONTROL('R'):
        ev = redo_event(SelFrame->buf);
        if (ev == NULL) {
            return 0;
        }
        SelFrame->cur = ev->cur;
        /* this is what the above text is referring to */
        SelFrame->cur.col = MIN(SelFrame->cur.col, get_mode_line_end(
                    &SelFrame->buf->lines[SelFrame->cur.line]));
        return 1;

    case 'A':
    case 'a':
    case 'I':
    case 'i':
        set_mode(INSERT_MODE);
        break;
    }
    return r + do_motion(SelFrame, motions[c]);
}
