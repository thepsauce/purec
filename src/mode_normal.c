#include "cmd.h"
#include "frame.h"
#include "buf.h"
#include "mode.h"
#include "util.h"

#include <ctype.h>
#include <ncurses.h>

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

        ['{'] = MOTION_PARA_UP,
        ['}'] = MOTION_PARA_DOWN,
    };

    int r = 0;
    struct buf *buf;
    struct pos cur, from, to;
    int e_c;
    struct undo_event *ev;

    buf = SelFrame->buf;
    switch (c) {
    case 'g':
        Mode.extra_counter = Mode.counter;
        Mode.counter = 0;
        e_c = getch_digit();
        Mode.counter = safe_mul(correct_counter(Mode.counter),
                correct_counter(Mode.extra_counter));
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
        set_mode(INSERT_MODE);
        /* fall through */
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
            to.line = safe_add(from.line, correct_counter(Mode.counter) - 1);
            to.col = SIZE_MAX;
            ev = delete_range(buf, &from, &to);
            ev->is_transient = true;
            indent_line(buf, from.line)->is_transient = true;
            from.col = buf->lines[from.line].n;
            set_cursor(SelFrame, &from);
            break;

        case 'd':
            if (c != 'd') {
                break;
            }

            to.line = safe_add(cur.line, correct_counter(Mode.counter));
            if (to.line > buf->num_lines) {
                to.line = buf->num_lines;
            }

            if (cur.line > 0) {
                from.line = cur.line - 1;
                from.col = buf->lines[from.line].n;
                to.line--;
                to.col = buf->lines[to.line].n;
            } else {
                from.line = cur.line;
                from.col = 0;
                to.col = 0;
            }

            ev = delete_range(buf, &from, &to);
            if (SelFrame->cur.line == buf->num_lines) {
                /* just go up once */
                Mode.counter = 0;
                do_motion(SelFrame, MOTION_UP);
            } else {
                clip_column(SelFrame);
            }
            break;

        default:
            Mode.type = INSERT_MODE;
            do_motion(SelFrame, motions[e_c]);
            Mode.type = NORMAL_MODE;
            ev = delete_range(buf, &cur, &SelFrame->cur);
            if (cur.line < SelFrame->cur.line ||
                    (cur.line == SelFrame->cur.line &&
                     cur.col < SelFrame->cur.col)) {
                SelFrame->cur = cur;
            }
            /* setting it to itself to clip it */
            set_cursor(SelFrame, &SelFrame->cur);
        }
        if (ev != NULL) {
            ev->undo_cur = cur;
            ev->redo_cur = SelFrame->cur;
            r = 1;
        }
        break;

    case 'x':
        cur = SelFrame->cur;
        cur.col += correct_counter(Mode.counter);
        ev = delete_range(buf, &SelFrame->cur, &cur);
        if (ev != NULL) {
            ev->undo_cur = cur;
            ev->redo_cur = SelFrame->cur;
            r = 1;
        }
        break;

    case 'X':
        cur = SelFrame->cur;
        move_horz(SelFrame, correct_counter(Mode.counter), -1);
        ev = delete_range(buf, &cur, &SelFrame->cur);
        if (ev != NULL) {
            ev->undo_cur = cur;
            ev->redo_cur = SelFrame->cur;
            r = 1;
        }
        break;

    case 'u':
        for (size_t i = 0; i < correct_counter(Mode.counter); i++) {
            ev = undo_event(buf);
            if (ev == NULL) {
                return 0;
            }
            set_cursor(SelFrame, &ev->undo_cur);
        }
        return 1;

    case CONTROL('R'):
        for (size_t i = 0; i < correct_counter(Mode.counter); i++) {
            ev = redo_event(buf);
            if (ev == NULL) {
                return 0;
            }
            set_cursor(SelFrame, &ev->redo_cur);
        }
        return 1;

    case 'O':
        cur = SelFrame->cur;
        set_mode(INSERT_MODE);
        do_motion(SelFrame, MOTION_HOME);
        ev = insert_text(buf, &SelFrame->cur, &(char) { '\n' }, 1, 1);
        ev->undo_cur = cur;
        ev->is_transient = true;
        indent_line(buf, SelFrame->cur.line);
        do_motion(SelFrame, MOTION_END);
        ev->redo_cur = SelFrame->cur;
        return 1;

    case 'o':
        cur = SelFrame->cur;
        set_mode(INSERT_MODE);
        do_motion(SelFrame, MOTION_END);
        ev = insert_text(buf, &SelFrame->cur, &(char) { '\n' }, 1, 1);
        ev->undo_cur = cur;
        ev->is_transient = true;
        indent_line(buf, SelFrame->cur.line + 1);
        do_motion(SelFrame, MOTION_DOWN);
        ev->redo_cur = SelFrame->cur;
        return 1;

    case ':':
        read_command_line();
        return 1;

    case 'v':
        set_mode(VISUAL_MODE);
        return 1;

    case 'V':
        set_mode(VISUAL_LINE_MODE);
        return 1;

    case CONTROL('V'):
        set_mode(VISUAL_BLOCK_MODE);
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
