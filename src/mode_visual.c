#include "buf.h"
#include "cmd.h"
#include "frame.h"
#include "purec.h"
#include "util.h"

#include <ctype.h>
#include <ncurses.h>

int visual_handle_input(int c)
{
    struct buf          *buf;
    struct pos          cur;
    int                 next_mode = NORMAL_MODE;
    struct selection    sel;
    struct undo_event   *ev;
    bool                line_based;
    struct raw_line     *lines;
    size_t              num_lines;

    buf = SelFrame->buf;
    switch (c) {
    /* go back to normal mode */
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
            (void) indent_line(buf, sel.beg.line);
            sel.beg.col = get_line_indent(buf, sel.beg.line);
        }

        set_mode(next_mode);
        if (ev != NULL) {
            yank_data(ev->seg, ev->flags);
            set_cursor(SelFrame, &sel.beg);
            return UPDATE_UI;
        }
        return 0;

    /* replace all characters under the selection */
    case 'r':
        ConvChar = get_ch();
        if (!isprint(ConvChar)) {
            return 0;
        }
        get_selection(&sel);
        if (sel.is_block) {
            (void) change_block(buf, &sel.beg, &sel.end, conv_to_char);
        } else {
            sel.end.col++;
            (void) change_range(buf, &sel.beg, &sel.end, conv_to_char);
        }
        set_mode(NORMAL_MODE);
        return UPDATE_UI;

    /* copy selected text */
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

        yank_data(save_lines(lines, num_lines), sel.is_block ? IS_BLOCK : 0);

        set_mode(NORMAL_MODE);
        return UPDATE_UI;

    /* convert selection to upper or lower case */
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
        return UPDATE_UI;

    /* jump to the corners of the selection */
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

    /* enter the command with the selected area */
    case ':':
        read_command_line(":'<,'>");
        return UPDATE_UI;

    /* enter visual mode or exit it */
    case 'v':
        set_mode(Core.mode == VISUAL_MODE ? NORMAL_MODE : VISUAL_MODE);
        return UPDATE_UI;

    /* enter visual line mode or exit it */
    case 'V':
        set_mode(Core.mode == VISUAL_LINE_MODE ? NORMAL_MODE :
                VISUAL_LINE_MODE);
        return UPDATE_UI;

    /* enter visual block mode or exit it */
    case CONTROL('V'):
        set_mode(Core.mode == VISUAL_BLOCK_MODE ? NORMAL_MODE :
                VISUAL_BLOCK_MODE);
        return UPDATE_UI;

    /* go to insert mode */
    case 'A':
    case 'I':
        get_selection(&sel);
        set_mode(INSERT_MODE);
        if (sel.is_block) {
            set_cursor(SelFrame, &sel.beg);
            Core.move_down_count = sel.end.line - sel.beg.line;
            if (c == 'A') {
                if (SelFrame->cur.col != buf->lines[SelFrame->cur.col].n) {
                    SelFrame->cur.col++;
                }
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

    /* indent all selected lines */
    case '=':
    case '<':
    case '>':
        get_selection(&sel);
        return do_action_till(SelFrame, c, sel.beg.line, sel.end.line);
    }
    return do_motion(SelFrame, c);
}
