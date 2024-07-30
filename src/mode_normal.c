#include "frame.h"
#include "buf.h"
#include "mode.h"
#include "util.h"

#include <ctype.h>
#include <ncurses.h>

static void clip_column(struct frame *frame)
{
    frame->cur.col = MIN(frame->cur.col, get_mode_line_end(
                &frame->buf->lines[frame->cur.line]));
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
        [' '] = MOTION_NEXT,

        [KEY_PPAGE] = MOTION_PAGE_UP,
        [KEY_NPAGE] = MOTION_PAGE_DOWN,
    };

    int r = 0;
    struct pos cur, from, to;
    int e_c;
    struct undo_event *ev;

    switch (c) {
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
        cur = SelFrame->cur;
        Mode.counter = safe_mul(correct_counter(Mode.counter),
                correct_counter(Mode.extra_counter));

        ev = NULL;
        switch (e_c) {
        case 'c':
            if (c != 'c') {
                break;
            }
            from.line = cur.line;
            from.col = 0;
            to.line = safe_add(cur.line, correct_counter(Mode.counter) - 1);
            to.col = SIZE_MAX;
            ev = delete_range(SelFrame->buf, &from, &to);
            SelFrame->cur = from;
            break;

        case 'd':
            if (c != 'd') {
                break;
            }
            from.line = cur.line;
            from.col = 0;
            to.line = safe_add(cur.line, correct_counter(Mode.counter));
            to.col = 0;
            ev = delete_range(SelFrame->buf, &from, &to);
            break;

        default:
            Mode.type = INSERT_MODE;
            do_motion(SelFrame, motions[e_c]);
            Mode.type = NORMAL_MODE;
            ev = delete_range(SelFrame->buf, &cur, &SelFrame->cur);
            if (cur.line < SelFrame->cur.line ||
                    (cur.line == SelFrame->cur.line &&
                     cur.col < SelFrame->cur.col)) {
                SelFrame->cur = cur;
            }
            clip_column(SelFrame);
        }
        if (ev != NULL) {
            ev->undo_cur = cur;
            ev->redo_cur = SelFrame->cur;
            r = 1;
        }

        if (c == 'c') {
            Mode.counter = 0;
            set_mode(INSERT_MODE);
            r = 1;
        }
        break;

    case 'x':
        cur = SelFrame->cur;
        cur.col += correct_counter(Mode.counter);
        ev = delete_range(SelFrame->buf, &SelFrame->cur, &cur);
        if (ev != NULL) {
            ev->undo_cur = cur;
            ev->redo_cur = SelFrame->cur;
            r = 1;
        }
        break;

    case 'X':
        cur = SelFrame->cur;
        move_horz(SelFrame, correct_counter(Mode.counter), -1);
        ev = delete_range(SelFrame->buf, &cur, &SelFrame->cur);
        if (ev != NULL) {
            ev->undo_cur = cur;
            ev->redo_cur = SelFrame->cur;
            r = 1;
        }
        break;

    case 'u':
        for (size_t i = 0; i < correct_counter(Mode.counter); i++) {
            ev = undo_event(SelFrame->buf);
            if (ev == NULL) {
                return 0;
            }
            SelFrame->cur = ev->undo_cur;
            /* since the event could have been made in insert mode,
             * we need to clip it. just like below in redo
             */
            clip_column(SelFrame);
        }
        return 1;

    case CONTROL('R'):
        for (size_t i = 0; i < correct_counter(Mode.counter); i++) {
            ev = redo_event(SelFrame->buf);
            if (ev == NULL) {
                return 0;
            }
            SelFrame->cur = ev->redo_cur;
            /* this is what the above text is referring to */
            clip_column(SelFrame);
        }
        return 1;

    case 'O':
        cur = SelFrame->cur;
        set_mode(INSERT_MODE);
        do_motion(SelFrame, MOTION_HOME);
        ev = insert_text(SelFrame->buf, &SelFrame->cur, &(char) { '\n' }, 1, 1);
        ev->undo_cur = cur;
        ev->is_transient = true;
        indent_line(SelFrame->buf, SelFrame->cur.line);
        do_motion(SelFrame, MOTION_END);
        ev->redo_cur = SelFrame->cur;
        return 1;

    case 'o':
        cur = SelFrame->cur;
        set_mode(INSERT_MODE);
        do_motion(SelFrame, MOTION_END);
        ev = insert_text(SelFrame->buf, &SelFrame->cur, &(char) { '\n' }, 1, 1);
        ev->undo_cur = cur;
        ev->is_transient = true;
        indent_line(SelFrame->buf, SelFrame->cur.line + 1);
        do_motion(SelFrame, MOTION_DOWN);
        ev->redo_cur = SelFrame->cur;
        return 1;

    case 'A':
    case 'a':
    case 'I':
    case 'i':
        set_mode(INSERT_MODE);
        break;
    }
    if (r == 0) {
        return do_motion(SelFrame, motions[c]);
    }
    return r;
}
