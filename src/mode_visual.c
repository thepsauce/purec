#include "cmd.h"
#include "frame.h"
#include "buf.h"
#include "mode.h"
#include "util.h"

#include <ctype.h>
#include <ncurses.h>

int visual_handle_input(int c)
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
    struct pos cur;
    int e_c;
    int next_mode = NORMAL_MODE;
    struct selection sel;
    struct undo_event *ev, *ev_o;
    bool line_based;

    switch (c) {
    case '\x1b':
        set_mode(NORMAL_MODE);
        return 1;

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
    case 'C':
    case 'c':
    case 'S':
    case 's':
        next_mode = INSERT_MODE;
        /* fall through */
    case 'D':
    case 'd':
    case 'X':
    case 'x':
        get_selection(&sel);
        if (sel.is_block) {
            line_based = false;
            if (c == 'D' || c == 'C') {
                sel.end.col = SIZE_MAX;
            } else if (c == 'X') {
                sel.beg.col = 0;
                sel.end.col = SIZE_MAX;
            }
            ev = delete_block(SelFrame->buf, &sel.beg, &sel.end);
        } else {
            if (Mode.type == VISUAL_MODE && isupper(c)) {
                /* upgrade to line deletion */
                sel.beg.col = 0;
                sel.end.col = 0;
                sel.end.line++;
                line_based = true;
            } else {
                line_based = Mode.type == VISUAL_LINE_MODE;
            }
            if ((c == 'C' || c == 'c') && line_based) {
                sel.end.line--;
                sel.end.col = SelFrame->buf->lines[sel.end.line].n;
            }
            ev = delete_range(SelFrame->buf, &sel.beg, &sel.end);
        }
        if ((c == 'C' || c == 'c') && line_based) {
            ev_o = indent_line(SelFrame->buf, sel.beg.line);
            if (ev_o != NULL) {
                ev->is_transient = true;
            }
            sel.beg.col = get_line_indent(SelFrame->buf, sel.beg.line);
        }
        set_mode(next_mode);
        if (ev != NULL) {
            ev->undo_cur = Mode.pos;
            ev->redo_cur = SelFrame->cur;
            set_cursor(SelFrame, &sel.beg);
            return 1;
        }
        break;

    case 'U':
    case 'u':
        /* TODO: case conversion */
        return 1;

    case 'O':
    case 'o':
        if (Mode.type == VISUAL_BLOCK_MODE && c == 'O') {
            cur.col = SelFrame->cur.col;
            SelFrame->cur.col = Mode.pos.col;
            Mode.pos.col = cur.col;
            return 1;
        }
        /* swap the cursor */
        cur = SelFrame->cur;
        SelFrame->cur = Mode.pos;
        Mode.pos = cur;
        return 1;

    case ':':
        read_command_line();
        return 1;

    case 'v':
        set_mode(Mode.type == VISUAL_MODE ? NORMAL_MODE : VISUAL_MODE);
        return 1;

    case 'V':
        set_mode(Mode.type == VISUAL_LINE_MODE ? NORMAL_MODE :
                VISUAL_LINE_MODE);
        return 1;

    case CONTROL('V'):
        set_mode(Mode.type == VISUAL_BLOCK_MODE ? NORMAL_MODE :
                VISUAL_BLOCK_MODE);
        return 1;

    case 'A':
    case 'I':
        /* TODO: multi line insert */
        set_mode(INSERT_MODE);
        return 1;
    }
    if (r == 0) {
        return do_motion(SelFrame, motions[c]);
    }
    return r;
}
