#include "buf.h"
#include "frame.h"
#include "purec.h"
#include "util.h"

#include <ctype.h>
#include <ncurses.h>

static int exit_visual_mode(void)
{
    set_mode(NORMAL_MODE);
    return UPDATE_UI;
}

static int enter_visual_mode(void)
{
    if (Core.mode == VISUAL_MODE) {
        set_mode(NORMAL_MODE);
    } else {
        set_mode(VISUAL_MODE);
    }
    return UPDATE_UI;
}

static int enter_visual_line_mode(void)
{
    if (Core.mode == VISUAL_LINE_MODE) {
        set_mode(NORMAL_MODE);
    } else {
        set_mode(VISUAL_LINE_MODE);
    }
    return UPDATE_UI;
}

static int enter_visual_block_mode(void)
{
    if (Core.mode == VISUAL_BLOCK_MODE) {
        set_mode(NORMAL_MODE);
    } else {
        set_mode(VISUAL_BLOCK_MODE);
    }
    return UPDATE_UI;
}

static int enter_append_mode(void)
{
    struct selection    sel;

    get_selection(&sel);
    set_mode(INSERT_MODE);
    if (sel.is_block) {
        set_cursor(SelFrame, &sel.beg);
        Core.move_down_count = sel.end.line - sel.beg.line;
        if (SelFrame->cur.col !=
                SelFrame->buf->text.lines[SelFrame->cur.col].n) {
            SelFrame->cur.col++;
        }
    } else {
        sel.end.col++;
        set_cursor(SelFrame, &sel.end);
    }
    return UPDATE_UI;
}

static int enter_insert_mode(void)
{
    struct selection    sel;

    get_selection(&sel);
    set_mode(INSERT_MODE);
    if (sel.is_block) {
        set_cursor(SelFrame, &sel.beg);
        Core.move_down_count = sel.end.line - sel.beg.line;
    } else {
        set_cursor(SelFrame, &sel.beg);
    }
    return UPDATE_UI;
}

static int enter_command_mode(void)
{
    read_command_line(":'<,'>");
    return UPDATE_UI;
}

static bool del_selection(bool chg)
{
    struct buf          *buf;
    struct selection    sel;
    struct undo_event   *ev;

    buf = SelFrame->buf;
    get_selection(&sel);
    if (sel.is_block) {
        ev = delete_block(buf, &sel.beg, &sel.end);
        Core.move_down_count = sel.end.line - sel.beg.line;
    } else {
        if (Core.mode == VISUAL_LINE_MODE) {
            if (chg) {
                sel.beg.col = 0;
                sel.end.col = buf->text.lines[sel.end.line].n;
            } else {
                sel.beg.col = 0;
                sel.end.col = 0;
                sel.end.line++;
            }
         } else if (!chg) {
              if (sel.end.col == buf->text.lines[sel.end.line].n) {
                 sel.end.line++;
                 sel.end.col = 0;
             } else {
                 sel.end.col++;
             }
        }
        ev = delete_range(buf, &sel.beg, &sel.end);
    }

    if (ev == NULL) {
        return false;
    }

    yank_data(ev->seg, ev->flags);
    set_cursor(SelFrame, &sel.beg);
    return true;
}

static int delete_selection(void)
{
    bool            b;

    b = del_selection(false);
    set_mode(NORMAL_MODE);
    return b ? UPDATE_UI : 0;
}

static int change_selection(void)
{
    bool            b;

    b = del_selection(true);
    set_mode(INSERT_MODE);
    return b ? UPDATE_UI : 0;
}

static bool del_selection_line_based(bool chg)
{
    struct buf          *buf;
    struct selection    sel;
    struct undo_event   *ev;

    buf = SelFrame->buf;
    get_selection(&sel);
    if (sel.is_block) {
        sel.beg.col = 0;
        sel.end.col = COL_MAX;
        ev = delete_block(buf, &sel.beg, &sel.end);
        Core.move_down_count = sel.end.line - sel.beg.line;
    } else {
        if (chg) {
            sel.beg.col = 0;
            sel.end.col = 0;
            sel.end.line++;
        } else {
            sel.beg.col = 0;
            sel.end.col = buf->text.lines[sel.end.line].n;
        }
        ev = delete_range(buf, &sel.beg, &sel.end);
    }

    if (ev == NULL) {
        return false;
    }
    yank_data(ev->seg, ev->flags);
    set_cursor(SelFrame, &sel.beg);
    return true;
}

static int delete_selection_line_based(void)
{
    bool            b;

    b = del_selection_line_based(false);
    set_mode(NORMAL_MODE);
    return b ? UPDATE_UI : 0;
}

static int change_selection_line_based(void)
{
    bool            b;

    b = del_selection_line_based(true);
    set_mode(INSERT_MODE);
    return b ? UPDATE_UI : 0;
}

static int convert_to_upper_case(void)
{
    struct selection    sel;
    struct undo_event   *ev;

    get_selection(&sel);
    if (sel.is_block) {
        ev = change_block(SelFrame->buf, &sel.beg, &sel.end, toupper);
    } else {
        sel.end.col++;
        ev = change_range(SelFrame->buf, &sel.beg, &sel.end, toupper);
    }
    set_mode(NORMAL_MODE);
    if (ev == NULL) {
        return 0;
    }
    return UPDATE_UI;
}

static int convert_to_lower_case(void)
{
    struct selection    sel;
    struct undo_event   *ev;

    get_selection(&sel);
    if (sel.is_block) {
        ev = change_block(SelFrame->buf, &sel.beg, &sel.end, tolower);
    } else {
        sel.end.col++;
        ev = change_range(SelFrame->buf, &sel.beg, &sel.end, tolower);
    }
    set_mode(NORMAL_MODE);
    if (ev == NULL) {
        return 0;
    }
    return UPDATE_UI;
}

static int yank_selection(void)
{
    struct selection    sel;
    struct text         text;

    get_selection(&sel);
    if (sel.is_block) {
        get_text_block(&SelFrame->buf->text, &sel.beg, &sel.end, &text);
    } else {
        if (Core.mode == VISUAL_LINE_MODE) {
            sel.beg.col = 0;
            sel.end.col = 0;
            sel.end.line++;
         } else {
              if (sel.end.col == SelFrame->buf->text.lines[sel.end.line].n) {
                 sel.end.line++;
                 sel.end.col = 0;
             } else {
                 sel.end.col++;
             }
        }
        get_text(&SelFrame->buf->text, &sel.beg, &sel.end, &text);
    }

    yank_data(save_lines(&text), sel.is_block ? IS_BLOCK : 0);

    set_mode(NORMAL_MODE);
    return UPDATE_UI;
}

static int swap_corner(void)
{
    col_t           col;

    col = SelFrame->cur.col;
    SelFrame->cur.col = Core.pos.col;
    Core.pos.col = col;
    return UPDATE_UI;
}

static int swap_cursor(void)
{
    struct pos      p;

    p = SelFrame->cur;
    SelFrame->cur = Core.pos;
    Core.pos = p;
    return UPDATE_UI;
}

static int reset_line_indent(void)
{
    struct selection    sel;

    get_selection(&sel);
    return do_action_till(SelFrame, '=', sel.beg.line, sel.end.line);
}

static int decrease_line_indent(void)
{
    struct selection    sel;

    get_selection(&sel);
    return do_action_till(SelFrame, '<', sel.beg.line, sel.end.line);
}

static int increase_line_indent(void)
{
    struct selection    sel;

    get_selection(&sel);
    return do_action_till(SelFrame, '>', sel.beg.line, sel.end.line);
}

int visual_handle_input(int c)
{
    static int (*const binds[])(void) = {
        ['\x1b']        = exit_visual_mode,
        [CONTROL('C')]  = exit_visual_mode,
        ['v']           = enter_visual_mode,
        ['V']           = enter_visual_line_mode,
        [CONTROL('V')]  = enter_visual_block_mode,
        ['I']           = enter_insert_mode,
        ['A']           = enter_append_mode,
        [':']           = enter_command_mode,
        ['x']           = delete_selection,
        ['d']           = delete_selection,
        ['D']           = delete_selection_line_based,
        ['X']           = delete_selection_line_based,
        ['c']           = change_selection,
        ['s']           = change_selection,
        ['C']           = change_selection_line_based,
        ['R']           = change_selection_line_based,
        ['U']           = convert_to_upper_case,
        ['u']           = convert_to_lower_case,
        ['Y']           = yank_selection,
        ['y']           = yank_selection,
        ['O']           = swap_corner,
        ['o']           = swap_cursor,
        ['=']           = reset_line_indent,
        ['<']           = decrease_line_indent,
        ['>']           = increase_line_indent,
    };

    if (c < (int) ARRAY_SIZE(binds) && binds[c] != NULL) {
        return binds[c]();
    }

    return do_motion(SelFrame, c);
}
