#include "xalloc.h"
#include "frame.h"
#include "buf.h"
#include "util.h"
#include "purec.h"

#include <ctype.h>
#include <string.h>
#include <ncurses.h>

/**
 * Checks if any events between the event at `Core.ev_from_ins` and the last
 * event can be combined and combines them by setting the `IS_TRANSIENT` flag.
 */
static void attempt_join(void)
{
    size_t              i;
    struct undo_event   *prev_ev, *ev;

    for (i = Core.ev_from_insert + 1; i < SelFrame->buf->event_i; i++) {
        prev_ev = &SelFrame->buf->events[i - 1];
        ev = &SelFrame->buf->events[i];
        if (should_join(prev_ev, ev)) {
            prev_ev->flags |= IS_TRANSIENT;
        }
    }
}
static void repeat_last_insertion(void)
{
    struct buf          *buf;
    size_t              e;
    line_t              i;
    struct undo_event   *ev;
    struct pos          cur;
    struct text         text;
    struct text         sub;
    struct pos          pos;
    struct pos          end;
    struct undo_seg     *seg;
    col_t               orig_col;
    size_t              repeat;

    buf = SelFrame->buf;

    if ((Core.repeat_insert <= 1 && Core.move_down_count == 0) ||
            Core.ev_from_insert == buf->event_i) {
        return;
    }

    /* check if there is only a SINGLE transient chain */
    for (e = Core.ev_from_insert + 1; e < buf->event_i; e++) {
        ev = &buf->events[e - 1];
        if (!(ev->flags & IS_TRANSIENT)) {
            return;
        }
    }

    /* combine the events (we only expect insertion and deletion events) */
    cur = buf->events[Core.ev_from_insert].pos;
    init_text(&text, 1);
    for (e = Core.ev_from_insert; e < buf->event_i; e++) {
        ev = &buf->events[e];
        pos = ev->pos;
        pos.line -= cur.line;
        if (pos.line == 0) {
            pos.col -= cur.col;
        }
        seg = ev->seg;
        sub.lines = seg->lines;
        sub.num_lines = seg->num_lines;
        load_undo_data(seg);
        if ((ev->flags & IS_INSERTION)) {
            insert_text(&text, &pos, &sub);
        } else {
            end = ev->end;
            end.line -= cur.line;
            if (end.line == 0) {
                end.col -= cur.col;
            }
            delete_text(&text, &pos, &end);
        }
        unload_undo_data(seg);
    }

    orig_col = cur.col;

    if (text.num_lines == 1) {
        cur.col += text.lines[0].n;
    } else {
        cur.line += text.num_lines - 1;
        cur.col = text.lines[text.num_lines - 1].n;
    }
    if (!is_point_equal(&SelFrame->cur, &cur) ||
            (text.num_lines == 1 && text.lines[0].n == 0)) {
        clear_text(&text);
        return;
    }

    buf->events[buf->event_i - 1].flags |= IS_TRANSIENT;
    repeat = Core.repeat_insert - 1;
    for (i = 0; i <= Core.move_down_count; i++, cur.line++) {
        if (cur.line >= SelFrame->buf->text.num_lines) {
            break;
        }
        if (orig_col > SelFrame->buf->text.lines[cur.line].n) {
            continue;
        }

        cur.col = orig_col;
        ev = insert_lines(SelFrame->buf, &cur, &text, repeat);

        if (text.num_lines == 1) {
            cur.col += text.lines[0].n;
        } else {
            cur.line += repeat * (text.num_lines - 1);
            cur.col = text.lines[text.num_lines - 1].n;
        }
        repeat = Core.repeat_insert;
    }
    cur.line--;

    set_cursor(SelFrame, &cur);

    clear_text(&text);
}

static int escape_insert_mode(void)
{
    attempt_join();
    repeat_last_insertion();
    Core.last_insert.buf = SelFrame->buf;
    Core.last_insert.pos = SelFrame->cur;
    (void) do_motion(SelFrame, KEY_LEFT);
    set_mode(NORMAL_MODE);
    return UPDATE_UI;
}

static int cancel_insert_mode(void)
{
    attempt_join();
    (void) do_motion(SelFrame, KEY_LEFT);
    set_mode(NORMAL_MODE);
    return UPDATE_UI;
}

static int enter_new_line(void)
{
    struct buf      *buf;

    buf = SelFrame->buf;
    if (buf->ev_last_indent + 1 == buf->event_i) {
        undo_event_no_trans(buf);
        if (buf->event_i > 0) {
            buf->events[buf->event_i].flags &= ~IS_TRANSIENT;
        }
        /* since the indentation is now trimmed, move to the first column */
        SelFrame->cur.col = 0;
    }
    (void) break_line(buf, &SelFrame->cur);
    set_cursor(SelFrame, &buf->events[buf->event_i - 1].end);
    return UPDATE_UI;
}

static int enter_tab(void)
{
    static struct line line;
    static struct text text = {
        .lines = &line,
        .num_lines = 1
    };

    char            ch;
    size_t          n;

    ch = ' ';
    n = Core.tab_size - SelFrame->cur.col % Core.tab_size;
    line.s = &ch;
    line.n = 1;
    (void) insert_lines(SelFrame->buf, &SelFrame->cur, &text, n);
    SelFrame->cur.col += n;
    SelFrame->vct = SelFrame->cur.col;
    (void) adjust_scroll(SelFrame);
    return UPDATE_UI;
}

static int delete_next_char(void)
{
    struct buf      *buf;

    buf = SelFrame->buf;
    if (SelFrame->cur.col == buf->text.lines[SelFrame->cur.line].n) {
        if (SelFrame->cur.line == buf->text.num_lines - 1) {
            return 0;
        }
        SelFrame->next_cur.line = SelFrame->cur.line + 1;
        SelFrame->next_cur.col = 0;
        SelFrame->next_vct = 0;
    } else {
        (void) prepare_motion(SelFrame, KEY_RIGHT);
    }
    (void) delete_range(buf, &SelFrame->cur, &SelFrame->next_cur);
    SelFrame->vct = SelFrame->next_vct;
    (void) adjust_scroll(SelFrame);
    return UPDATE_UI;
}

static int delete_prev_char(void)
{
    struct buf          *buf;
    struct line         *line;
    col_t               i;

    buf = SelFrame->buf;
    SelFrame->next_cur = SelFrame->cur;
    if (SelFrame->cur.col == 0) {
        if (SelFrame->cur.line == 0) {
            return 0;
        }
        SelFrame->next_cur.line--;
        SelFrame->next_cur.col = buf->text.lines[SelFrame->next_cur.line].n;
        SelFrame->next_vct = compute_vct(SelFrame, &SelFrame->next_cur);
    } else {
        if (SelFrame->cur.col % Core.tab_size == 0) {
            Core.counter = Core.tab_size;
       
            line = &buf->text.lines[SelFrame->cur.line];
            for (i = SelFrame->cur.col;
                 i > SelFrame->cur.col - Core.tab_size; ) {
                i--;
                if (line->s[i] != ' ') {
                    Core.counter = 1;
                    break;
                }
            }
        } else {
            Core.counter = 1;
        }
        (void) prepare_motion(SelFrame, KEY_LEFT);
    }
    (void) delete_range(buf, &SelFrame->next_cur, &SelFrame->cur);
    SelFrame->cur = SelFrame->next_cur;
    SelFrame->vct = SelFrame->next_vct;
    (void) adjust_scroll(SelFrame);
    return UPDATE_UI;
}


int insert_handle_input(int c)
{
    static struct line line;
    static struct text text = {
        .lines = &line,
        .num_lines = 1
    };

    char ch;

    static int (*binds[KEY_MAX])(void) = {
        ['\x1b'] = escape_insert_mode,
        [CONTROL('C')] = cancel_insert_mode,
        ['\n'] = enter_new_line,
        ['\t'] = enter_tab,
        [KEY_DC] = delete_next_char,
        [0x7f] = delete_prev_char,
        [KEY_BACKSPACE] = delete_prev_char,
        ['\b'] = delete_prev_char,
    };

    if (binds[c] != NULL) {
        return binds[c]();
    }

    if (c >= ' ' && c < 0x100) {
        ch = c;
        line.s = &ch;
        line.n = 1;
        (void) insert_lines(SelFrame->buf, &SelFrame->cur, &text, 1);
        SelFrame->cur.col++;
        if (SelFrame->cur.col ==
                SelFrame->buf->text.lines[SelFrame->cur.line].n &&
                (ch == '}' || ch == ':')) {
            (void) indent_line(SelFrame->buf, SelFrame->cur.line);
            SelFrame->cur.col = SelFrame->buf->text.lines[SelFrame->cur.line].n;
        }
        SelFrame->vct = SelFrame->cur.col;
        (void) adjust_scroll(SelFrame);
        return UPDATE_UI;
   }
   return do_motion(SelFrame, c);
}
