#include "buf.h"
#include "frame.h"
#include "fuzzy.h"
#include "purec.h"
#include "xalloc.h"

#include <ctype.h>
#include <limits.h>
#include <ncurses.h>
#include <string.h>

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
    struct buf *buf;
    struct pos cur, scroll;
    size_t vct;

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

static inline int bind_to_motion(int action)
{
    int             c;
    struct buf      *buf;
    line_t          min_line, max_line;

    c = get_extra_char();
    buf = SelFrame->buf;
    if (c == action) {
        min_line = SelFrame->cur.line;
        max_line = safe_add(min_line, Core.counter - 1);
        max_line = MIN(max_line, buf->num_lines - 1);
    } else {
        if (prepare_motion(SelFrame, c) == 0) {
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

int normal_handle_input(int c)
{
    int                 r = 0;
    struct buf          *buf;
    struct pos          cur, from, to;
    int                 e_c;
    struct undo_event   *ev, *ev_nn;
    struct raw_line     lines[2];
    struct frame        *frame;
    char                *entry;
    int                 next_mode = NORMAL_MODE;
    struct raw_line     *d_lines;
    line_t              num_lines;
    struct reg          *reg;
    struct mark         *mark;
    struct play_rec     *rec;
    char                ch[2];

    buf = SelFrame->buf;
    switch (c) {
    /* reset the bottom status message */
    case '\x1b':
        if (Core.msg_state != MSG_DEFAULT) {
            Core.msg_state = MSG_TO_DEFAULT;
            return UPDATE_UI;
        }
        return 0;

    /* change or delete */
    case 'c':
        next_mode = INSERT_MODE;
        /* fall through */
    case 'd':
        e_c = get_extra_char();
        /* fall through */
    case 'C':
    case 'D':
    case 'S':
        if (c == 'D' || c == 'C') {
            if (c == 'C') {
                next_mode = INSERT_MODE;
            }
            c = 'd';
            e_c = '$';
        } else if (c == 'S') {
            c = 'c';
            e_c = 'c';
        }
        cur = SelFrame->cur;
        ev = NULL;
        switch (e_c) {
        case 'c':
            if (c != 'c') {
                break;
            }
            from.line = cur.line;
            from.col = 0;
            to.line = safe_add(from.line, Core.counter - 1);
            to.col = COL_MAX;
            ev = delete_range(buf, &from, &to);
            ev_nn = indent_line(buf, from.line);
            if (ev_nn != NULL && ev != NULL) {
                /* since the base pointer might change, this is to correct for
                 * that
                 */
                ev = ev_nn - 1;
            }
            SelFrame->cur.col = buf->lines[from.line].n;
            break;

        case 'd':
            if (c != 'd') {
                break;
            }

            to.line = safe_add(cur.line, Core.counter);
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
                SelFrame->cur.line--;
            }
            /* `set_mode()` clips the column */
            break;

        default:
            r = prepare_motion(SelFrame, e_c);
            if (r == 0) {
                return 0;
            }
            from = SelFrame->cur;
            to = SelFrame->next_cur;
            if (r == 2) {
                to.col++;
            }
            ev = delete_range(buf, &from, &to);
            if (to.line < SelFrame->cur.line ||
                (to.line == SelFrame->cur.line &&
                 to.col < SelFrame->cur.col)) {
                SelFrame->cur = to;
            }
        }
        if (ev != NULL) {
            yank_data(ev->seg, 0);
        }
        (void) adjust_scroll(SelFrame);
        if (next_mode == INSERT_MODE) {
            Core.counter = 1;
            set_mode(next_mode);
        }
        clip_column(SelFrame);
        return UPDATE_UI | DO_RECORD;

    /* delete current character on the current line */
    case KEY_DC:
    case 'x':
    case 's': /* also enter insert mode */
        if (prepare_motion(SelFrame, KEY_RIGHT) == 0) {
            return 0;
        }
        ev = delete_range(buf, &SelFrame->cur, &SelFrame->next_cur);
        if (ev != NULL) {
            yank_data(ev->seg, 0);
            r = UPDATE_UI;
        }
        if (c == 's') {
            Core.counter = 1;
            set_mode(INSERT_MODE);
            r = UPDATE_UI;
        } else {
            clip_column(SelFrame);
        }
        SelFrame->vct = SelFrame->next_vct;
        (void) adjust_scroll(SelFrame);
        return r | DO_RECORD;

    /* delete the previous character on the current line */
    case 'X':
        if (prepare_motion(SelFrame, KEY_LEFT) == 0) {
            return 0;
        }
        ev = delete_range(buf, &SelFrame->next_cur, &SelFrame->cur);
        yank_data(ev->seg, 0);
        SelFrame->cur = SelFrame->next_cur;
        SelFrame->vct = SelFrame->next_vct;
        (void) adjust_scroll(SelFrame);
        return UPDATE_UI | DO_RECORD;

    /* join lines */
    case CONTROL('J'):
        from = SelFrame->cur;
        for (; Core.counter > 0; Core.counter--) {
            if (from.line + 1 == buf->num_lines) {
                break;
            }
            from.col = buf->lines[from.line].n;
            to = from;
            to.line++;
            to.col = get_line_indent(buf, to.line);
            ev = delete_range(buf, &from, &to);
            if (from.col > 0 && from.col != buf->lines[from.line].n &&
                    !isblank(buf->lines[from.line].s[from.col - 1])) {
                init_raw_line(&lines[0], " ", 1);
                (void) insert_lines(buf, &from, lines, 1, 1);
                free(lines[0].s);
            }
        }
        return UPDATE_UI | DO_RECORD;

    /* replace current character */
    case 'r':
        c = get_ch();
        if (!isprint(c) && c != '\n' && c != '\t') {
            return 0;
        }
        cur = SelFrame->cur;
        cur.col = safe_add(cur.col, Core.counter);
        if (c == '\n' || c == '\t') {
            ev = delete_range(buf, &SelFrame->cur, &cur);
            return insert_handle_input(c);
        }
        ConvChar = c;
        ev = change_range(buf, &SelFrame->cur, &cur, conv_to_char);
        if (ev == NULL) {
            return 0;
        }
        return UPDATE_UI | DO_RECORD;

    /* undo last event in current frame */
    case 'u':
        ev_nn = NULL;
        for (; Core.counter > 0; Core.counter--) {
            ev = undo_event(buf);
            if (ev == NULL) {
                break;
            }
            ev_nn = ev;
        }
        if (ev_nn != NULL) {
            set_cursor(SelFrame, &ev_nn->cur);
            return UPDATE_UI;
        }
        return 0;

    /* redo last undone event in current frame */
    case CONTROL('R'):
        ev_nn = NULL;
        for (; Core.counter > 0; Core.counter--) {
            ev = redo_event(buf);
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
            return UPDATE_UI;
        }
        return 0;

    /* insert line above and enter insert mode */
    case 'O':
        set_mode(INSERT_MODE);
        cur = SelFrame->cur;
        if (cur.line == 0) {
            lines[0].n = 0;
            lines[1].n = 0;
            cur.col = 0;
            ev = insert_lines(buf, &cur, lines, 2, 1);
            clip_column(SelFrame);
        } else {
            cur.line--;
            cur.col = buf->lines[cur.line].n;
            ev = break_line(buf, &cur);
            set_cursor(SelFrame, &buf->events[buf->event_i - 1].end);
        }
        return UPDATE_UI | DO_RECORD;

    /* insert line below and enter insert mode */
    case 'o':
        set_mode(INSERT_MODE);
        cur.line = SelFrame->cur.line;
        cur.col = buf->lines[cur.line].n;
        ev = break_line(buf, &cur);
        set_cursor(SelFrame, &buf->events[buf->event_i - 1].end);
        return UPDATE_UI | DO_RECORD;

    /* enter command */
    case ':':
        read_command_line(":");
        return UPDATE_UI;

    /* enter search */
    case '/':
    case '?':
        ch[0] = c;
        ch[1] = '\0';
        read_command_line(ch);
        return UPDATE_UI;

    /* show message that C-C does not exit */
    case CONTROL('C'):
        set_message("Type  :qa!<ENTER>  to quit and abandon all changes");
        return UPDATE_UI;

    /* enter visual mode */
    case 'v':
        set_mode(VISUAL_MODE);
        return UPDATE_UI;

    /* enter visual line mode */
    case 'V':
        set_mode(VISUAL_LINE_MODE);
        return UPDATE_UI;

    /* enter visual block mode */
    case CONTROL('V'):
        set_mode(VISUAL_BLOCK_MODE);
        return UPDATE_UI;

    /* do a frame command */
    case CONTROL('W'):
        e_c = get_extra_char();
        switch (e_c) {
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
            frame = do_binded_frame_movement(tolower(e_c));
            return exchange_frames(SelFrame, frame);

        /* exchange current frame with frame of given frame movement */
        case CONTROL('X'):
        case 'x':
        case 'X':
            e_c = get_extra_char();
            if (c >= 1 && c <= 26) {
                c += 'a' - 1;
            }
            frame = do_binded_frame_movement(e_c);
            r = exchange_frames(SelFrame, frame);
            if (e_c == 'X') {
                SelFrame = frame;
                set_cursor(SelFrame, &SelFrame->cur);
            }
            return r;

        /* expand the width of current frame */
        case '>':
            return move_right_edge(SelFrame, MIN(INT_MAX, Core.counter)) > 0;

        /* move current frame to the left and expand its width */
        case '<':
            return move_left_edge(SelFrame, MIN(INT_MAX, Core.counter)) > 0;

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
            /* TODO */
            return 0;
        }

        frame = do_binded_frame_movement(e_c);
        if (frame == SelFrame) {
            return 0;
        }
        if (SelFrame->buf == frame->buf) {
            /* clip the cursor */
            set_cursor(frame, &frame->cur);
        }
        SelFrame = frame;
        return UPDATE_UI;

    /* repeat last action */
    case '.':
        rec = &Core.rec_stack[Core.rec_stack_n++];
        rec->from = Core.dot.from;
        rec->to = Core.dot.to;
        rec->index = rec->from;
        rec->repeat_count = Core.counter - 1;
        return 0;

    case '=':
    case '<':
    case '>':
        return bind_to_motion(c);

    /* enter insert mode and ... */
    case 'A': /* ...go to the end of the line */
    case 'a': /* ...go one to the right */
    case 'I': /* ...go to the start of the line and skip space */
    case 'i': /* ...do nothing */
        set_mode(INSERT_MODE);
        (void) do_motion(SelFrame, c);
        return UPDATE_UI | DO_RECORD;

    /* start a recording or end the current recording */
    case 'q':
        if (Core.user_rec_ch != '\0') {
            /* minus 1 to exclude the 'q' */
            Core.user_recs[Core.user_rec_ch - USER_REC_MIN].to =
                Core.rec_len - 1;
            Core.user_rec_ch = '\0';
            Core.msg_state = MSG_TO_DEFAULT;
            return UPDATE_UI;
        }
        c = toupper(get_ch());
        if (c < USER_REC_MIN || c > USER_REC_MAX) {
            break;
        }
        Core.user_rec_ch = c;
        Core.user_recs[Core.user_rec_ch - USER_REC_MIN].from = Core.rec_len;
        Core.msg_state = MSG_TO_DEFAULT;
        return UPDATE_UI;

    /* replay a recording */
    case '@':
        c = toupper(get_ch());
        if (c < USER_REC_MIN || c > USER_REC_MAX) {
            break;
        }
        if (Core.user_recs[c - USER_REC_MIN].from >=
                Core.user_recs[c - USER_REC_MIN].to) {
            set_error("recording %c is empty", c);
            return UPDATE_UI;
        }
        if (Core.rec_stack_n == ARRAY_SIZE(Core.rec_stack)) {
            set_error("overflow error on recording stack, playback is stopped");
            Core.rec_stack_n = 0;
            return UPDATE_UI;
        }
        rec = &Core.rec_stack[Core.rec_stack_n++];
        rec->from = Core.user_recs[c - USER_REC_MIN].from;
        rec->to = Core.user_recs[c - USER_REC_MIN].to;
        rec->index = rec->from;
        rec->repeat_count = Core.counter - 1;
        return UPDATE_UI;

    /* yank text to a register */
    case 'Y':
    case 'y':
        if (c == 'Y') {
            e_c = '$';
        } else {
            e_c = get_extra_char();
        }
        cur = SelFrame->cur;
        ev = NULL;
        switch (e_c) {
        /* yank entire line */
        case 'y':
            to.line = safe_add(cur.line, Core.counter);
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
            break;

        /* yank till motion */
        default:
            if (prepare_motion(SelFrame, e_c) == 0) {
                return 0;
            }
            from = SelFrame->cur;
            to = SelFrame->next_cur;
            sort_positions(&from, &to);
        }
        d_lines = get_lines(buf, &from, &to, &num_lines);
        yank_data(save_lines(d_lines, num_lines), 0);
        return UPDATE_UI;

    /* paste text from a register */
    case 'P':
    case 'p':
        cur = SelFrame->cur;
        if (c == 'p') {
            cur.col++;
            cur.col = MIN(cur.col, buf->lines[cur.line].n);
        }

        if (Core.user_reg == '+' || Core.user_reg == '*') {
            d_lines = paste_clipboard(&num_lines, Core.user_reg == '*');
            if (d_lines == NULL) {
                return 0;
            }
            ev = insert_lines(buf, &cur, d_lines, num_lines, Core.counter);
        } else {
            reg = &Core.regs[Core.user_reg - '.'];
            if (reg->seg == NULL) {
                return 0;
            }
            load_undo_data(reg->seg);
            if ((reg->flags & IS_BLOCK)) {
                ev = insert_block(buf, &cur, reg->seg->lines,
                        reg->seg->num_lines, Core.counter);
            } else {
                ev = insert_lines(buf, &cur, reg->seg->lines,
                        reg->seg->num_lines, Core.counter);
            }
            unload_undo_data(reg->seg);
        }
        set_cursor(SelFrame, &ev->end);
        return UPDATE_UI;

    /* set a mark at the current cursor position */
    case 'm':
        c = toupper(get_ch());
        if (c == '\x1b') {
            return 0;
        }
        if (c < MARK_MIN || c > MARK_MAX) {
            set_error("invalid mark");
            return UPDATE_UI;
        }
        mark = &Core.marks[c - MARK_MIN];
        mark->buf = SelFrame->buf;
        mark->pos = SelFrame->cur;
        return UPDATE_UI;

    /* go to a mark */
    case '`':
    case '\'':
        c = toupper(get_ch());
        if (c == '\x1b') {
            return 0;
        }
        if (c == '?') {
            /* TODO: show window with all marks */
            return 0;
        }
        mark = get_mark(SelFrame, c);
        if (mark == NULL) {
            set_error("invalid mark");
            return UPDATE_UI;
        }
        if (SelFrame->buf == mark->buf) {
            set_cursor(SelFrame, &mark->pos);
            return UPDATE_UI;
        }
        for (frame = FirstFrame; frame != NULL; frame = frame->next) {
            if (frame->buf == mark->buf) {
                SelFrame = frame;
                set_cursor(frame, &mark->pos);
                return UPDATE_UI;
            }
        }
        if (buf != mark->buf) {
            set_frame_buffer(SelFrame, mark->buf);
        }
        set_cursor(SelFrame, &mark->pos);
        return 0;

    /* scroll up {counter} times */
    case 'K':
        return scroll_frame(SelFrame, -Core.counter);

    /* scroll down {counter} times */
    case 'J':
        return scroll_frame(SelFrame, Core.counter);

    /* scroll up half the frame {counter} times */
    case CONTROL('U'):
        Core.counter = safe_mul(Core.counter, MAX(SelFrame->h / 2, 1));
        return scroll_frame(SelFrame, -Core.counter);

    /* scroll down half the frame {counter} times */
    case CONTROL('D'):
        Core.counter = safe_mul(Core.counter, MAX(SelFrame->h / 2, 1));
        return scroll_frame(SelFrame, Core.counter);

    /* open a file in the fuzzy file dialog */
    case 'Z':
        entry = choose_file(NULL);
        if (entry != NULL) {
            buf = create_buffer(entry);
            set_frame_buffer(SelFrame, buf);
        }
        return UPDATE_UI;

    case CONTROL('S'):
        choose_session();
        return UPDATE_UI;
    }
    /* do a motion, see `get_binded_motion()` */
    return prepare_motion(SelFrame, c) && apply_motion(SelFrame);
}
