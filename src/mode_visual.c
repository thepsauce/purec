#include "buf.h"
#include "cmd.h"
#include "frame.h"
#include "purec.h"
#include "util.h"

#include <ctype.h>
#include <ncurses.h>

int visual_handle_input(int c)
{
    int r = 0;
    struct buf *buf;
    struct pos cur;
    int next_mode = NORMAL_MODE;
    struct selection sel;
    struct undo_event *ev, *ev_o;
    bool line_based;
    struct raw_line *lines;
    size_t num_lines;

    buf = SelFrame->buf;
    switch (c) {
    case '\x1b':
    case CONTROL('C'):
        set_mode(NORMAL_MODE);
        return UPDATE_UI;

    /* change or delete */
    case 'R':
        c = 'C';
        /* fall through */
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
                Core.move_down_count = sel.end.line - sel.beg.line;
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
            ev->cur = Core.pos;
            yank_data(ev->data_i, ev->flags);
            set_cursor(SelFrame, &sel.beg);
            return UPDATE_UI;
        }
        break;

    case 'Y':
    case 'y':
        get_selection(&sel);
        if (sel.is_block) {
            lines = get_block(buf, &sel.beg, &sel.end, &num_lines);
        } else {
            if (Core.mode == VISUAL_MODE && isupper(c)) {
                /* upgrade to line yanking */
                sel.beg.col = 0;
                sel.end.col = 0;
                sel.end.line++;
            } else if (Core.mode != VISUAL_LINE_MODE) {
                if (sel.end.col == buf->lines[sel.end.line].n) {
                    sel.end.line++;
                    sel.end.col = 0;
                } else {
                    sel.end.col++;
                }
            }
            lines = get_lines(buf, &sel.beg, &sel.end, &num_lines);
        }

        yank_lines(lines, num_lines, sel.is_block ? IS_BLOCK : 0);

        set_mode(NORMAL_MODE);
        return UPDATE_UI;

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
        ev->cur = SelFrame->cur;
        return UPDATE_UI;

    case 'O':
    case 'o':
        if (Core.mode == VISUAL_BLOCK_MODE && c == 'O') {
            cur.col = SelFrame->cur.col;
            SelFrame->cur.col = Core.pos.col;
            Core.pos.col = cur.col;
            return UPDATE_UI;
        }
        /* swap the cursor */
        cur = SelFrame->cur;
        SelFrame->cur = Core.pos;
        Core.pos = cur;
        return UPDATE_UI;

    case ':':
        read_command_line(":'<,'>");
        return UPDATE_UI;

    case 'v':
        set_mode(Core.mode == VISUAL_MODE ? NORMAL_MODE : VISUAL_MODE);
        return UPDATE_UI;

    case 'V':
        set_mode(Core.mode == VISUAL_LINE_MODE ? NORMAL_MODE :
                VISUAL_LINE_MODE);
        return UPDATE_UI;

    case CONTROL('V'):
        set_mode(Core.mode == VISUAL_BLOCK_MODE ? NORMAL_MODE :
                VISUAL_BLOCK_MODE);
        return UPDATE_UI;

    case 'A':
    case 'I':
        get_selection(&sel);
        set_mode(INSERT_MODE);
        if (sel.is_block) {
            set_cursor(SelFrame, &sel.beg);
            Core.move_down_count = sel.end.line - sel.beg.line;
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
        }
        return UPDATE_UI;
    }
    if (r == 0) {
        return do_motion(SelFrame, get_binded_motion(c));
    }
    return r;
}
