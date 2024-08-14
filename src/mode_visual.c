#include "buf.h"
#include "cmd.h"
#include "frame.h"
#include "purec.h"
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
    struct pos cur;
    int e_c;
    int next_mode = NORMAL_MODE;
    struct selection sel;
    struct undo_event *ev, *ev_o;
    bool line_based;

    buf = SelFrame->buf;
    switch (c) {
    case '\x1b':
        set_mode(NORMAL_MODE);
        return 1;

    case 'g':
        e_c = get_extra_char();
        switch (e_c) {
        case 'G':
        case 'g':
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
            ev = delete_block(buf, &sel.beg, &sel.end);
            if (c == 'C' || c == 'c') {
                Core.move_down_count = sel.end.line - sel.beg.line + 1;
            }
        } else {
            if (Core.mode == VISUAL_MODE && isupper(c)) {
                /* upgrade to line deletion */
                sel.beg.col = 0;
                sel.end.col = 0;
                sel.end.line++;
                line_based = true;
            } else if (Core.mode == VISUAL_LINE_MODE) {
                line_based = true;
            } else {
                line_based = false;
                if (sel.end.col == buf->lines[sel.end.line].n) {
                    sel.end.line++;
                    sel.end.col = 0;
                } else {
                    sel.end.col++;
                }
            }
            if ((c == 'C' || c == 'c') && line_based) {
                sel.end.line--;
                sel.end.col = buf->lines[sel.end.line].n;
            }
            ev = delete_range(buf, &sel.beg, &sel.end);
        }

        if ((c == 'C' || c == 'c') && line_based) {
            ev_o = indent_line(buf, sel.beg.line);
            if (ev_o != NULL) {
                ev->flags |= IS_TRANSIENT;
            }
            sel.beg.col = get_line_indent(buf, sel.beg.line);
        } else {
            ev_o = NULL;
        }

        set_mode(next_mode);
        if (ev != NULL) {
            ev->undo_cur = Core.pos;
            if (ev_o != NULL) {
                ev_o->redo_cur = sel.beg;
            } else {
                ev->redo_cur = sel.beg;
            }
            set_cursor(SelFrame, &sel.beg);
            return 1;
        }
        break;

    case 'U':
    case 'u':
        get_selection(&sel);
        if (sel.is_block) {
            ev = change_block(buf, &sel.beg, &sel.end,
                    c == 'U' ? toupper : tolower);
        } else {
            sel.end.col++;
            ev = change_range(buf, &sel.beg, &sel.end,
                    c == 'U' ? toupper : tolower);
        }
        set_mode(NORMAL_MODE);
        if (ev == NULL) {
            return 0;
        }
        ev->undo_cur = SelFrame->cur;
        ev->redo_cur = Core.pos;
        return 1;

    case 'O':
    case 'o':
        if (Core.mode == VISUAL_BLOCK_MODE && c == 'O') {
            cur.col = SelFrame->cur.col;
            SelFrame->cur.col = Core.pos.col;
            Core.pos.col = cur.col;
            return 1;
        }
        /* swap the cursor */
        cur = SelFrame->cur;
        SelFrame->cur = Core.pos;
        Core.pos = cur;
        return 1;

    case ':':
        read_command_line(":'<,'>");
        return 1;

    case 'v':
        set_mode(Core.mode == VISUAL_MODE ? NORMAL_MODE : VISUAL_MODE);
        return 1;

    case 'V':
        set_mode(Core.mode == VISUAL_LINE_MODE ? NORMAL_MODE :
                VISUAL_LINE_MODE);
        return 1;

    case CONTROL('V'):
        set_mode(Core.mode == VISUAL_BLOCK_MODE ? NORMAL_MODE :
                VISUAL_BLOCK_MODE);
        return 1;

    case 'A':
    case 'I':
        get_selection(&sel);
        set_mode(INSERT_MODE);
        if (sel.is_block) {
            set_cursor(SelFrame, &sel.beg);
            if (Core.play_i == SIZE_MAX) {
                Core.move_down_count = sel.end.line - sel.beg.line;
                Core.st_repeat_count = Core.counter - 1;
                Core.repeat_count = Core.st_repeat_count;
                Core.play_i = Core.dot_i;
            }
            if (c == 'A') {
                move_horz(SelFrame, 1, 1);
            }
        } else {
            if (c == 'A') {
                sel.end.col++;
                set_cursor(SelFrame, &sel.end);
            } else {
                set_cursor(SelFrame, &sel.beg);
            }
            Core.repeat_count = Core.counter - 1;
        }
        return 1;
    }
    if (r == 0) {
        return do_motion(SelFrame, motions[c]);
    }
    return r;
}
