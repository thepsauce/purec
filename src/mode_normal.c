#include "buf.h"
#include "frame.h"
#include "fuzzy.h"
#include "purec.h"
#include "xalloc.h"

#include <ctype.h>
#include <limits.h>
#include <ncurses.h>
#include <string.h>

static int escape_normal_mode(void)
{
    if (Core.msg_state != MSG_DEFAULT) {
        Core.msg_state = MSG_TO_DEFAULT;
        return UPDATE_UI;
    }
    return 0;
}

static int notify_how_to_quit(void)
{
    set_message("Type  :qa!<ENTER>  to quit and abandon all changes");
    return UPDATE_UI;
}

static int enter_insert_mode(void)
{
    set_mode(INSERT_MODE);
    return UPDATE_UI;
}

static int enter_append_mode(void)
{
    set_mode(INSERT_MODE);
    (void) do_motion(SelFrame, KEY_RIGHT);
    return UPDATE_UI;
}

static int enter_insert_beg_mode(void)
{
    set_mode(INSERT_MODE);
    (void) do_motion(SelFrame, 'I');
    return UPDATE_UI;
}

static int enter_append_end_mode(void)
{
    set_mode(INSERT_MODE);
    (void) do_motion(SelFrame, KEY_END);
    return UPDATE_UI;
}

static int enter_visual_mode(void)
{
    set_mode(VISUAL_MODE);
    return UPDATE_UI;
}

static int enter_visual_line_mode(void)
{
    set_mode(VISUAL_LINE_MODE);
    return UPDATE_UI;
}

static int enter_visual_block_mode(void)
{
    set_mode(VISUAL_BLOCK_MODE);
    return UPDATE_UI;
}

static int enter_command_mode(void)
{
    read_command_line(":");
    return UPDATE_UI;
}

static int enter_search_mode(void)
{
    read_command_line("/");
    return UPDATE_UI;
}

static int enter_reverse_search_mode(void)
{
    read_command_line("?");
    return UPDATE_UI;
}

/**
 * Swaps given frames.
 *
 * @param a The first frame.
 * @param b The second frame.
 *
 * @return 0 if the frames are the same, otherwise 1.
 */
static int exchange_frames(struct frame *a, struct frame *b)
{
    struct buf      *buf;
    struct pos      cur, scroll;
    size_t          vct;

    if (a == b) {
        return 0;
    }

    buf = a->buf;
    cur = a->cur;
    scroll = a->scroll;
    vct = a->vct;

    a->buf = b->buf;
    a->cur = b->cur;
    a->scroll = b->scroll;
    a->vct = b->vct;

    b->buf = buf;
    b->cur = cur;
    b->scroll = scroll;
    b->vct = vct;
    return UPDATE_UI;
}

/**
 * Does the motion binded to given input character.
 *
 * @param c The input character.
 *
 * @return The frame resulting from the movement.
 */
static struct frame *do_binded_frame_movement(int c)
{
    struct frame    *frame, *f;
    int             x, y;

    switch (c) {
    /* go to a previous window in the linked list */
    case CONTROL('P'):
    case 'p':
        Core.counter %= get_frame_count();
        for (frame = FirstFrame; Core.counter > 0; Core.counter--) {
            while (frame->next != NULL && frame->next != SelFrame) {
                frame = frame->next;
            }
        }
        return frame;

    /* go to a next window int the linked list */
    case CONTROL('N'):
    case 'n':
        Core.counter %= get_frame_count();
        for (frame = SelFrame; Core.counter > 0; Core.counter--) {
            frame = frame->next;
            if (frame == NULL) {
                frame = FirstFrame;
            }
        }
        return frame;

    /* go to a frame on the left */
    case CONTROL('H'):
    case 'h':
        for (frame = SelFrame; Core.counter > 0; Core.counter--) {
            if (!get_visual_pos(frame, &frame->cur, &x, &y)) {
                y = frame->y;
            }
            f = get_frame_at(frame->x - 1, y);
            if (f == NULL) {
                break;
            }
            frame = f;
        }
        return frame;

    /* go to a frame below */
    case CONTROL('J'):
    case 'j':
        for (frame = SelFrame; Core.counter > 0; Core.counter--) {
            if (!get_visual_pos(frame, &frame->cur, &x, &y)) {
                x = frame->x;
            }
            f = get_frame_at(x, frame->y + frame->h);
            if (f == NULL) {
                break;
            }
            frame = f;
        }
        return frame;

    /* goto to a frame above */
    case CONTROL('K'):
    case 'k':
        for (frame = SelFrame; Core.counter > 0; Core.counter--) {
            if (!get_visual_pos(frame, &frame->cur, &x, &y)) {
                x = frame->x;
            }
            f = get_frame_at(x, frame->y - 1);
            if (f == NULL) {
                break;
            }
            frame = f;
        }
        return frame;

    /* goto to a frame on the right */
    case CONTROL('L'):
    case 'l':
        for (frame = SelFrame; Core.counter > 0; Core.counter--) {
            if (!get_visual_pos(frame, &frame->cur, &x, &y)) {
                y = frame->y;
            }
            f = get_frame_at(frame->x + frame->w, y);
            if (f == NULL) {
                break;
            }
            frame = f;
        }
        return frame;

    /* go to the frame in the top left and then right */
    case CONTROL('T'):
    case 't':
        frame = get_frame_at(0, 0);
        for (; Core.counter > 1; Core.counter--) {
            f = get_frame_at(frame->x + frame->w, 0);
            if (f == NULL) {
                break;
            }
            frame = f;
        }
        return frame;

    /* go to the frame in the bottom right and then left */
    case CONTROL('B'):
    case 'b':
        frame = get_frame_at(COLS - 1, LINES - 2);
        for (; Core.counter > 1; Core.counter--) {
            f = get_frame_at(frame->x - 1, 0);
            if (f == NULL) {
                break;
            }
            frame = f;
        }
        return frame;
    }
    return SelFrame;
}

static int do_frame_command(void)
{
    int             c, e_c;
    struct frame    *frame;
    int             r;

    c = get_extra_char();
    switch (c) {
    /* close the current window */
    case CONTROL('Q'):
    case 'q':
    case CONTROL('C'):
    case 'c':
        destroy_frame(SelFrame);
        return UPDATE_UI;

    /* make the current frame the only visible frame */
    case CONTROL('O'):
    case 'o':
        if (FirstFrame->next == NULL) {
            return 0;
        }
        set_only_frame(SelFrame);
        return UPDATE_UI;

    /* create new frame below */
    case 'N':
        SelFrame = create_frame(SelFrame, SPLIT_DOWN, NULL);
        return UPDATE_UI;

    /* split frame on the right */
    case CONTROL('V'):
    case 'v':
    case 'V': /* also move to it */
        frame = create_frame(SelFrame, SPLIT_RIGHT, SelFrame->buf);
        set_cursor(frame, &SelFrame->cur);
        if (c == 'V') {
            SelFrame = frame;
        }
        return UPDATE_UI;

    /* split frame below */
    case CONTROL('S'):
    case 's':
    case 'S': /* also move to it */
        frame = create_frame(SelFrame, SPLIT_DOWN, SelFrame->buf);
        set_cursor(frame, &SelFrame->cur);
        if (c == 'S') {
            SelFrame = frame;
        }
        return UPDATE_UI;

    /* exchange current frame with the ... */
    case 'H': /* ...frame on the left */
    case 'J': /* ...frame below */
    case 'K': /* ...frame above */
    case 'L': /* ...frame on the right */
        frame = do_binded_frame_movement(tolower(c));
        return exchange_frames(SelFrame, frame);

    /* exchange current frame with frame of given frame movement */
    case CONTROL('X'):
    case 'x':
    case 'X':
        e_c = get_extra_char();
        /* updrade CONTROL(X) */
        if (e_c >= 1 && e_c <= 26) {
            e_c += 'a' - 1;
        }
        frame = do_binded_frame_movement(e_c);
        r = exchange_frames(SelFrame, frame);
        if (c == 'X') {
            SelFrame = frame;
            set_cursor(SelFrame, &SelFrame->cur);
        }
        return r;

    /* expand the width of current frame */
    case '>':
        return move_right_edge(SelFrame, MIN(Core.counter, INT_MAX)) > 0;

    /* move current frame to the left and expand its width */
    case '<':
        return move_left_edge(SelFrame, MIN(Core.counter, INT_MAX)) > 0;

    /* set the width */
    case '|':
        /* TODO: */
        return 0;

    /* analogous to ><| but for the height */
    case '+':
    case '-':
    case '_':
        /* TODO: */
        return 0;

    /* jump to the file under the cursor */
    case CONTROL('F'):
    case 'f':
        jump_to_file(SelFrame->buf, &SelFrame->cur);
        return UPDATE_UI;
    }

    frame = do_binded_frame_movement(c);
    if (frame == SelFrame) {
        return DO_NOT_RECORD;
    }
    if (SelFrame->buf == frame->buf) {
        /* clip the cursor */
        set_cursor(frame, &frame->cur);
    }
    SelFrame = frame;
    return UPDATE_UI | DO_NOT_RECORD;
}

static int choose_fuzzy_file(void)
{
    char            *file;
    struct buf      *buf;

    file = choose_file(NULL);
    if (file != NULL) {
        buf = create_buffer(file);
        set_frame_buffer(SelFrame, buf);
        free(file);
    }
    return UPDATE_UI | DO_NOT_RECORD;
}

static int choose_fuzzy_session(void)
{
    choose_session();
    return UPDATE_UI | DO_NOT_RECORD;
}

static int scroll_up(void)
{
    return scroll_frame(SelFrame, -MIN(Core.counter, LINE_MAX)) |
            DO_NOT_RECORD;
}

static int scroll_down(void)
{
    return scroll_frame(SelFrame, MIN(Core.counter, LINE_MAX)) |
            DO_NOT_RECORD;
}

static int scroll_up_half(void)
{
    Core.counter = safe_mul(Core.counter, MAX(SelFrame->h / 2, 1));
    return scroll_frame(SelFrame, -MIN(Core.counter, LINE_MAX)) |
            DO_NOT_RECORD;
}

static int scroll_down_half(void)
{
    Core.counter = safe_mul(Core.counter, MAX(SelFrame->h / 2, 1));
    return scroll_frame(SelFrame, MIN(Core.counter, LINE_MAX)) |
            DO_NOT_RECORD;
}

static int goto_mark(void)
{
    int             c;
    struct mark     *mark;
    struct frame    *frame;

    c = toupper(get_ch());
    if (c == '\x1b') {
        return DO_NOT_RECORD;
    }
    if (c == '?') {
        /* TODO: show window with all marks */
        return DO_NOT_RECORD;
    }
    mark = get_mark(SelFrame, c);
    if (mark == NULL) {
        set_error("invalid mark");
        return UPDATE_UI | DO_NOT_RECORD;
    }
    if (SelFrame->buf == mark->buf) {
        set_cursor(SelFrame, &mark->pos);
        return UPDATE_UI | DO_NOT_RECORD;
    }
    frame = get_frame_with_buffer(mark->buf);
    if (frame != NULL) {
        SelFrame = frame;
        set_cursor(frame, &mark->pos);
    } else {
        set_frame_buffer(SelFrame, mark->buf);
        set_cursor(SelFrame, &mark->pos);
    }
    return UPDATE_UI | DO_NOT_RECORD;
}

static int place_mark(void)
{
    int             c;
    struct mark     *mark;

    c = toupper(get_ch());
    if (c == '\x1b') {
        return 0;
    }
    if (c < MARK_MIN || c > MARK_MAX) {
        set_error("invalid mark");
        return UPDATE_UI | DO_NOT_RECORD;
    }
    mark = &Core.marks[c - MARK_MIN];
    mark->buf = SelFrame->buf;
    mark->pos = SelFrame->cur;
    return UPDATE_UI | DO_NOT_RECORD;
}

static int do_undo(void)
{
    struct undo_event   *ev, *ev_nn;

    ev_nn = NULL;
    for (; Core.counter > 0; Core.counter--) {
        ev = undo_event(SelFrame->buf);
        if (ev == NULL) {
            break;
        }
        ev_nn = ev;
    }
    if (ev_nn != NULL) {
        set_cursor(SelFrame, &ev_nn->cur);
        return UPDATE_UI | DO_NOT_RECORD;
    }
    return DO_NOT_RECORD;
}

static int do_redo(void)
{
    struct undo_event   *ev, *ev_nn;

    ev_nn = NULL;
    for (; Core.counter > 0; Core.counter--) {
        ev = redo_event(SelFrame->buf);
        if (ev == NULL) {
            break;
        }
        ev_nn = ev;
    }
    if (ev_nn != NULL) {
        if ((ev_nn->flags & IS_DELETION)) {
            set_cursor(SelFrame, &ev_nn->cur);
        } else {
            set_cursor(SelFrame, &ev_nn->end);
        }
        return UPDATE_UI | DO_NOT_RECORD;
    }
    return DO_NOT_RECORD;
}

static int insert_line_above(void)
{
    struct pos          p;
    struct text         text;

    set_mode(INSERT_MODE);
    p = SelFrame->cur;
    if (p.line == 0) {
        p.col = 0;
        init_text(&text, 2);
        (void) insert_lines(SelFrame->buf, &p, &text, 1);
        clear_text(&text);
        clip_column(SelFrame);
    } else {
        p.line--;
        p.col = SelFrame->buf->text.lines[p.line].n;
        (void) break_line(SelFrame->buf, &p);
        p = SelFrame->buf->events[SelFrame->buf->event_i - 1].end;
        set_cursor(SelFrame, &p);
    }
    return UPDATE_UI;
}

static int insert_line_below(void)
{
    struct pos          p;

    set_mode(INSERT_MODE);
    p.line = SelFrame->cur.line;
    p.col = SelFrame->buf->text.lines[p.line].n;
    (void) break_line(SelFrame->buf, &p);
    p = SelFrame->buf->events[SelFrame->buf->event_i - 1].end;
    set_cursor(SelFrame, &p);
    return UPDATE_UI;
}

static int delete_chars(void)
{
    struct undo_event   *ev;

    if (prepare_motion(SelFrame, KEY_RIGHT) <= 0) {
        return DO_NOT_RECORD;
    }
    ev = delete_range(SelFrame->buf, &SelFrame->cur, &SelFrame->next_cur);
    if (ev != NULL) {
        yank_data(ev->seg, 0);
    }
    clip_column(SelFrame);
    SelFrame->vct = compute_vct(SelFrame, &SelFrame->cur);
    (void) adjust_scroll(SelFrame);
    return UPDATE_UI;
}

static int replace_chars(void)
{
    struct undo_event   *ev;

    if (prepare_motion(SelFrame, KEY_RIGHT) <= 0) {
        return DO_NOT_RECORD;
    }
    ev = delete_range(SelFrame->buf, &SelFrame->cur, &SelFrame->next_cur);
    if (ev != NULL) {
        yank_data(ev->seg, 0);
    }
    Core.counter = 1;
    set_mode(INSERT_MODE);
    clip_column(SelFrame);
    SelFrame->vct = compute_vct(SelFrame, &SelFrame->cur);
    (void) adjust_scroll(SelFrame);
    return UPDATE_UI;
}

static int delete_prev_chars(void)
{
    struct undo_event   *ev;
    if (prepare_motion(SelFrame, KEY_LEFT) <= 0) {
        return DO_NOT_RECORD;
    }
    ev = delete_range(SelFrame->buf, &SelFrame->next_cur, &SelFrame->cur);
    yank_data(ev->seg, 0);
    SelFrame->cur = SelFrame->next_cur;
    SelFrame->vct = SelFrame->next_vct;
    (void) adjust_scroll(SelFrame);
    return UPDATE_UI;
}

static int delete_till(int c)
{
    struct pos          cur, from, to;
    struct undo_event   *ev, *ev_nn;
    int                 r;

    cur = SelFrame->cur;
    ev = NULL;
    switch (c) {
    case 'c':
        from.line = cur.line;
        from.col = 0;
        to.line = safe_add(from.line, Core.counter - 1);
        to.col = COL_MAX;
        ev = delete_range(SelFrame->buf, &from, &to);
        ev_nn = indent_line(SelFrame->buf, from.line);
        if (ev_nn != NULL && ev != NULL) {
            /* since the base pointer might change, this is to correct for
             * that
             */
            ev = ev_nn - 1;
        }
        SelFrame->cur.col = SelFrame->buf->text.lines[from.line].n;
        break;

    case 'd':
        to.line = safe_add(cur.line, Core.counter);
        if (to.line > SelFrame->buf->text.num_lines) {
            to.line = SelFrame->buf->text.num_lines;
        }

        if (cur.line > 0) {
            from.line = cur.line - 1;
            from.col = SelFrame->buf->text.lines[from.line].n;
            to.line--;
            to.col = SelFrame->buf->text.lines[to.line].n;
        } else {
            from.line = cur.line;
            from.col = 0;
            to.col = 0;
        }

        ev = delete_range(SelFrame->buf, &from, &to);
        if (SelFrame->cur.line == SelFrame->buf->text.num_lines) {
            SelFrame->cur.line--;
        }
        /* `set_mode()` clips the column */
        break;

    default:
        r = prepare_motion(SelFrame, c);
        if (r <= 0) {
            return r;
        }
        from = SelFrame->cur;
        to = SelFrame->next_cur;
        if (r == 2) {
            to.col = move_forward_glyph(SelFrame->buf->text.lines[to.line].s,
                                        to.col,
                                        SelFrame->buf->text.lines[to.line].n);
        }
        ev = delete_range(SelFrame->buf, &from, &to);
        if (to.line < SelFrame->cur.line ||
                (to.line == SelFrame->cur.line && to.col < SelFrame->cur.col)) {
            SelFrame->cur = to;
        }
    }
    if (ev != NULL) {
        yank_data(ev->seg, 0);
    }
    return UPDATE_UI;
}

static int change_lines(void)
{
    if (delete_till(get_extra_char()) == -1) {
        return DO_NOT_RECORD;
    }
    Core.counter = 1;
    set_mode(INSERT_MODE);
    clip_column(SelFrame);
    (void) adjust_scroll(SelFrame);
    return UPDATE_UI;
}

static int delete_lines(void)
{
    if (delete_till(get_extra_char()) <= 0) {
        return DO_NOT_RECORD;
    }
    clip_column(SelFrame);
    (void) adjust_scroll(SelFrame);
    return UPDATE_UI;
}

static int change_to_end(void)
{
    (void) delete_till('$');
    Core.counter = 1;
    set_mode(INSERT_MODE);
    clip_column(SelFrame);
    (void) adjust_scroll(SelFrame);
    return UPDATE_UI;
}

static int delete_to_end(void)
{
    (void) delete_till('$');
    clip_column(SelFrame);
    (void) adjust_scroll(SelFrame);
    return UPDATE_UI;
}

static int change_entire_lines(void)
{
    (void) delete_till('c');
    Core.counter = 1;
    set_mode(INSERT_MODE);
    clip_column(SelFrame);
    (void) adjust_scroll(SelFrame);
    return UPDATE_UI;
}

static int yank_lines_to(int c)
{
    struct pos      from, to;
    struct text     text;

    switch (c) {
    /* yank entire line */
    case 'y':
        to.line = safe_add(SelFrame->cur.line, Core.counter);
        if (to.line > SelFrame->buf->text.num_lines) {
            to.line = SelFrame->buf->text.num_lines;
        }

        if (SelFrame->cur.line > 0) {
            from.line = SelFrame->cur.line - 1;
            from.col = SelFrame->buf->text.lines[from.line].n;
            to.line--;
            to.col = SelFrame->buf->text.lines[to.line].n;
        } else {
            from.line = SelFrame->cur.line;
            from.col = 0;
            to.col = 0;
        }
        break;

    /* yank till motion */
    default:
        if (prepare_motion(SelFrame, c) <= 0) {
            return DO_NOT_RECORD;
        }
        from = SelFrame->cur;
        to = SelFrame->next_cur;
        sort_positions(&from, &to);
    }
    get_text(&SelFrame->buf->text, &from, &to, &text);
    yank_data(save_lines(&text), 0);
    return UPDATE_UI | DO_NOT_RECORD;
}

static int yank_to_end(void)
{
    return yank_lines_to('$');
}

static int yank_lines(void)
{
    return yank_lines_to(get_extra_char());
}

static int paste_text(bool before)
{
    struct pos          p;
    struct text         text;
    struct undo_event   *ev;
    struct reg          *reg;

    p = SelFrame->cur;
    if (!before && p.col < SelFrame->buf->text.lines[p.line].n) {
        p.col = move_forward_glyph(SelFrame->buf->text.lines[p.line].s,
                                   p.col, SelFrame->buf->text.lines[p.line].n);
    }

    if (Core.user_reg == '+' || Core.user_reg == '*') {
        if (!paste_clipboard(&text, Core.user_reg == '*')) {
            return 0;
        }
        ev = insert_lines(SelFrame->buf, &p, &text, Core.counter);
    } else {
        reg = &Core.regs[Core.user_reg - '.'];
        if (reg->seg == NULL) {
            return 0;
        }
        load_undo_data(reg->seg);
        make_text(&text, reg->seg->lines, reg->seg->num_lines);
        if ((reg->flags & IS_BLOCK)) {
            ev = insert_block(SelFrame->buf, &p, &text, Core.counter);
        } else {
            ev = insert_lines(SelFrame->buf, &p, &text, Core.counter);
        }
        unload_undo_data(reg->seg);
    }
    set_cursor(SelFrame, &ev->end);
    return UPDATE_UI;
}

static int paste_lines_before(void)
{
    return paste_text(true);
}

static int paste_lines(void)
{
    return paste_text(false);
}

static int replace_current_char(void)
{
    int                 c;

    c = get_ch();
    if (!isprint(c) && c != '\n' && c != '\t') {
        return 0;
    }
    if (prepare_motion(SelFrame, KEY_RIGHT) <= 0) {
        return 0;
    }
    if (c == '\n' || c == '\t') {
        (void) delete_range(SelFrame->buf, &SelFrame->cur, &SelFrame->next_cur);
        return insert_handle_input(c);
    }
    ConvChar = c;
    (void) change_range(SelFrame->buf, &SelFrame->cur, &SelFrame->next_cur,
                      conv_to_char);
    return UPDATE_UI;
}

static int join_lines(void)
{
    struct pos      from, to;
    struct text     text;
    struct line     *line;

    from = SelFrame->cur;
    for (; Core.counter > 0; Core.counter--) {
        if (from.line + 1 == SelFrame->buf->text.num_lines) {
            break;
        }
        from.col = SelFrame->buf->text.lines[from.line].n;
        to = from;
        to.line++;
        (void) get_line_indent(SelFrame->buf, to.line, &to.col);
        (void) delete_range(SelFrame->buf, &from, &to);
        line = &SelFrame->buf->text.lines[from.line];
        if (from.col > 0 && from.col != line->n &&
                !isblank(line->s[from.col - 1])) {
            init_text(&text, 1);
            init_line(&text.lines[0], " ", 1);
            (void) insert_lines(SelFrame->buf, &from, &text, 1);
            clear_text(&text);
        }
    }
    return UPDATE_UI;
}

static inline int bind_to_motion(int action)
{
    int             c;
    int             r;
    line_t          min_line, max_line;

    c = get_extra_char();
    if (c == action) {
        min_line = SelFrame->cur.line;
        max_line = safe_add(min_line, Core.counter - 1);
        max_line = MIN(max_line, SelFrame->buf->text.num_lines - 1);
    } else {
        r = prepare_motion(SelFrame, c);
        if (r == -1) {
            return 0;
        }
        if (r == 0) {
            min_line = SelFrame->cur.line;
            max_line = min_line;
        } else if (SelFrame->cur.line < SelFrame->next_cur.line) {
            min_line = SelFrame->cur.line;
            max_line = SelFrame->next_cur.line;
        } else {
            min_line = SelFrame->next_cur.line;
            max_line = SelFrame->cur.line;
        }
    }
    return do_action_till(SelFrame, action, min_line, max_line);
}

static int indent_lines(void)
{
    return bind_to_motion('=');
}

static int decrease_line_indent(void)
{
    return bind_to_motion('<');
}

static int increase_line_indent(void)
{
    return bind_to_motion('>');
}

static int do_recording(void)
{
    int             c;

    if (Core.user_rec_ch != '\0') {
        /* minus 1 to exclude the 'q' */
        Core.user_recs[Core.user_rec_ch - USER_REC_MIN].to =
            Core.rec_len - 1;
        Core.user_rec_ch = '\0';
        Core.msg_state = MSG_TO_DEFAULT;
        return UPDATE_UI | DO_NOT_RECORD;
    }
    c = toupper(get_ch());
    if (c < USER_REC_MIN || c > USER_REC_MAX) {
        return DO_NOT_RECORD;
    }
    Core.user_rec_ch = c;
    Core.user_recs[Core.user_rec_ch - USER_REC_MIN].from = Core.rec_len;
    Core.msg_state = MSG_TO_DEFAULT;
    return UPDATE_UI | DO_NOT_RECORD;
}

static int play_recording(void)
{
    int             c;
    struct play_rec *rec;

    c = toupper(get_ch());
    if (c < USER_REC_MIN || c > USER_REC_MAX) {
        return DO_NOT_RECORD;
    }
    if (Core.user_recs[c - USER_REC_MIN].from >=
            Core.user_recs[c - USER_REC_MIN].to) {
        set_error("recording %c is empty", c);
        return UPDATE_UI | DO_NOT_RECORD;
    }
    if (Core.rec_stack_n == ARRAY_SIZE(Core.rec_stack)) {
        set_error("overflow error on recording stack, playback is stopped");
        Core.rec_stack_n = 0;
        return UPDATE_UI | DO_NOT_RECORD;
    }
    rec = &Core.rec_stack[Core.rec_stack_n++];
    rec->from = Core.user_recs[c - USER_REC_MIN].from;
    rec->to = Core.user_recs[c - USER_REC_MIN].to;
    rec->index = rec->from;
    rec->repeat_count = Core.counter - 1;
    return UPDATE_UI | DO_NOT_RECORD;
}

static int play_dot_recording(void)
{
    struct play_rec *rec;

    if (Core.rec_stack_n == ARRAY_SIZE(Core.rec_stack)) {
        set_error("overflow error on recording stack, playback is stopped");
        Core.rec_stack_n = 0;
        return UPDATE_UI | DO_NOT_RECORD;
    }
    rec = &Core.rec_stack[Core.rec_stack_n++];
    rec->from = Core.dot.from;
    rec->to = Core.dot.to;
    rec->index = rec->from;
    rec->repeat_count = Core.counter - 1;
    return DO_NOT_RECORD;
}

int normal_handle_input(int c)
{
    static int (*const binds[])(void) = {
        ['\x1b']        = escape_normal_mode,
        [CONTROL('C')]  = notify_how_to_quit,
        ['i']           = enter_insert_mode,
        ['a']           = enter_append_mode,
        ['I']           = enter_insert_beg_mode,
        ['A']           = enter_append_end_mode,
        ['v']           = enter_visual_mode,
        ['V']           = enter_visual_line_mode,
        [CONTROL('V')]  = enter_visual_block_mode,
        [':']           = enter_command_mode,
        ['/']           = enter_search_mode,
        ['?']           = enter_reverse_search_mode,
        [CONTROL('W')]  = do_frame_command,
        ['Z']           = choose_fuzzy_file,
        [CONTROL('S')]  = choose_fuzzy_session,
        ['K']           = scroll_up,
        ['J']           = scroll_down,
        [CONTROL('U')]  = scroll_up_half,
        [CONTROL('D')]  = scroll_down_half,
        ['\'']          = goto_mark,
        ['`']           = goto_mark,
        ['M']           = place_mark,
        ['m']           = place_mark,
        ['u']           = do_undo,
        [CONTROL('R')]  = do_redo,
        ['O']           = insert_line_above,
        ['o']           = insert_line_below,
        [KEY_DC]        = delete_chars,
        ['x']           = delete_chars,
        ['s']           = replace_chars,
        ['X']           = delete_prev_chars,
        ['c']           = change_lines,
        ['C']           = change_to_end,
        ['d']           = delete_lines,
        ['D']           = delete_to_end,
        ['S']           = change_entire_lines,
        ['Y']           = yank_to_end,
        ['y']           = yank_lines,
        ['P']           = paste_lines_before,
        ['p']           = paste_lines,
        ['r']           = replace_current_char,
        [CONTROL('J')]  = join_lines,
        ['=']           = indent_lines,
        ['<']           = decrease_line_indent,
        ['>']           = increase_line_indent,
        ['q']           = do_recording,
        ['@']           = play_recording,
        ['.']           = play_dot_recording,
    };

    if (c < (int) ARRAY_SIZE(binds) && binds[c] != NULL) {
        return binds[c]();
    }
    return do_motion(SelFrame, c) | DO_NOT_RECORD;
}
