#include "buf.h"
#include "cmd.h"
#include "frame.h"
#include "fuzzy.h"
#include "purec.h"
#include "xalloc.h"

#include <ctype.h>
#include <ncurses.h>
#include <string.h>

int ConvChar;

int conv_to_char(int c)
{
    (void) c;
    return ConvChar;
}

int normal_handle_input(int c)
{
    int r = 0;
    struct buf *buf;
    struct pos cur, from, to;
    int e_c;
    struct undo_event *ev, *ev_nn;
    struct raw_line lines[2];
    struct frame *frame;
    struct file_list file_list;
    size_t entry;
    int next_mode = NORMAL_MODE;
    struct raw_line *d_lines;
    size_t num_lines;
    struct reg *reg;
    struct undo_seg *seg;
    int motion;
    struct mark *mark;

    buf = SelFrame->buf;
    switch (c) {
    case '\x1b':
        if (Core.msg_state != MSG_DEFAULT) {
            Core.msg_state = MSG_TO_DEFAULT;
            return UPDATE_UI;
        }
        return 0;

    case 'J':
        return scroll_frame(SelFrame, Core.counter, 1);

    case 'K':
        return scroll_frame(SelFrame, Core.counter, -1);

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
            to.col = SIZE_MAX;
            ev = delete_range(buf, &from, &to);
            (void) indent_line(buf, from.line);
            from.col = buf->lines[from.line].n;
            set_cursor(SelFrame, &from);
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
                move_vert(SelFrame, 1, -1);
            } else {
                clip_column(SelFrame);
            }
            break;

        default:
            motion = get_binded_motion(e_c);
            do_motion(SelFrame, motion);
            from = SelFrame->cur;
            to = cur;
            sort_positions(&from, &to);
            /* some motions do not want increment, this is to make some motions
             * seem more natural
             */
            switch (motion) {
            case MOTION_NEXT_WORD:
            case MOTION_PREV_WORD:
            case MOTION_UP:
            case MOTION_DOWN:
            case MOTION_LEFT:
            case MOTION_RIGHT:
            case MOTION_PREV:
            case MOTION_NEXT:
            case MOTION_FILE_BEG:
            case MOTION_FILE_END:
                break;

            default:
                to.col++;
            }
            ev = delete_range(buf, &from, &to);
            set_cursor(SelFrame, &from);
        }
        if (ev != NULL) {
            yank_data(ev->data_i, 0);
            ev->cur = cur;
            r = UPDATE_UI;
        }
        r |= DO_RECORD;
        set_mode(next_mode);
        return r;

    case 'x':
    case 's':
        cur = SelFrame->cur;
        cur.col = safe_add(cur.col, Core.counter);
        ev = delete_range(buf, &SelFrame->cur, &cur);
        if (ev != NULL) {
            yank_data(ev->data_i, 0);
            ev->cur = SelFrame->cur;
            r = UPDATE_UI;
        }
        if (c == 's') {
            set_mode(INSERT_MODE);
            r = UPDATE_UI;
        } else {
            clip_column(SelFrame);
        }
        r |= DO_RECORD;
        return r;

    case 'X':
        cur = SelFrame->cur;
        (void) move_horz(SelFrame, Core.counter, -1);
        ev = delete_range(buf, &cur, &SelFrame->cur);
        if (ev != NULL) {
            yank_data(ev->data_i, 0);
            ev->cur = cur;
            return UPDATE_UI | DO_RECORD;
        }
        return 0;

    case CONTROL('J'):
        from = SelFrame->cur;
        for (size_t i = 0; i < Core.counter; i++) {
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
            ev->cur = SelFrame->cur;
        }
        return UPDATE_UI;

    case 'r':
        c = get_ch();
        if (!isprint(c) && c != '\n') {
            return 0;
        }
        cur = SelFrame->cur;
        cur.col = safe_add(cur.col, Core.counter);
        if (c == '\n') {
            ev = delete_range(buf, &SelFrame->cur, &cur);
            if (ev != NULL) {
                ev->cur = SelFrame->cur;
                ev->flags |= IS_TRANSIENT;
            }
            ev = break_line(buf, &SelFrame->cur);
        } else {
            ConvChar = c;
            ev = change_range(buf, &SelFrame->cur, &cur, conv_to_char);
            ev->cur = SelFrame->cur;
        }
        ev->cur = SelFrame->cur;
        set_cursor(SelFrame, &cur);
        return UPDATE_UI | DO_RECORD;

    case 'u':
        ev_nn = NULL;
        for (size_t i = 0; i < Core.counter; i++) {
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

    case CONTROL('R'):
        ev_nn = NULL;
        for (size_t i = 0; i < Core.counter; i++) {
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

    case 'O':
        set_mode(INSERT_MODE);
        cur = SelFrame->cur;
        if (cur.line == 0) {
            lines[0].n = 0;
            lines[1].n = 0;
            ev = insert_lines(buf, &cur, lines, 2, 1);
            ev->cur = cur;
            clip_column(SelFrame);
        } else {
            cur.line--;
            cur.col = buf->lines[cur.line].n;
            ev = break_line(buf, &cur);
            ev->cur = SelFrame->cur;
            set_cursor(SelFrame, &SelFrame->cur);
        }
        return UPDATE_UI | DO_RECORD;

    case 'o':
        set_mode(INSERT_MODE);
        cur.line = SelFrame->cur.line;
        cur.col = buf->lines[cur.line].n;
        ev = break_line(buf, &cur);
        ev->cur = SelFrame->cur;
        set_cursor(SelFrame, &ev->end);
        return UPDATE_UI | DO_RECORD;

    case ':':
        read_command_line(":");
        return UPDATE_UI;

    case CONTROL('C'):
        set_message("Type  :qa!<ENTER>  to quit and abandon all changes");
        return UPDATE_UI;

    case 'v':
        set_mode(VISUAL_MODE);
        return UPDATE_UI | DO_RECORD;

    case 'V':
        set_mode(VISUAL_LINE_MODE);
        return UPDATE_UI | DO_RECORD;

    case CONTROL('W'):
        e_c = get_extra_char();

        frame = SelFrame;
        switch (e_c) {
        case CONTROL('Q'):
        case 'q':
            destroy_frame(SelFrame);
            frame = SelFrame;
            break;

        case CONTROL('P'):
        case 'p':
            for (frame = FirstFrame; frame->next != NULL &&
                    frame->next != SelFrame; ) {
                frame = frame->next;
            }
            frame = frame->next == NULL ? FirstFrame : frame;
            break;

        case CONTROL('N'):
        case 'n':
            frame = SelFrame->next;
            break;

        case CONTROL('H'):
        case 'h':
            for (frame = SelFrame; Core.counter > 0; Core.counter--) {
                frame = get_frame_at(SelFrame->x - 1, SelFrame->y);
            }
            break;

        case CONTROL('J'):
        case 'j':
            for (frame = SelFrame; Core.counter > 0; Core.counter--) {
                frame = get_frame_at(frame->x, frame->y + frame->h);
            }
            break;

        case CONTROL('K'):
        case 'k':
            for (frame = SelFrame; Core.counter > 0; Core.counter--) {
                frame = get_frame_at(SelFrame->x, SelFrame->y - 1);
            }
            break;

        case CONTROL('L'):
        case 'l':
            for (frame = SelFrame; Core.counter > 0; Core.counter--) {
                frame = get_frame_at(SelFrame->x + SelFrame->w, SelFrame->y);
            }
            break;

        case CONTROL('V'):
        case 'v':
            (void) create_frame(SelFrame, SPLIT_RIGHT, SelFrame->buf);
            break;

        case CONTROL('S'):
        case 's':
            (void) create_frame(SelFrame, SPLIT_DOWN, SelFrame->buf);
            break;
        }
        if (frame != NULL) {
            SelFrame = frame;
        }
        return UPDATE_UI;

    case CONTROL('V'):
        set_mode(VISUAL_BLOCK_MODE);
        return UPDATE_UI | DO_RECORD;

    case '.':
        if (Core.dot_i >= Core.rec_len - 1) {
            return 0;
        }
        if (Core.dot_e == Core.rec_len) {
            /* exclude the '.' itself */
            Core.dot_e--;
        }
        Core.repeat_count = Core.counter;
        return 0;

    case 'A':
    case 'a':
    case 'I':
    case 'i':
        set_mode(INSERT_MODE);
        if (c == 'A') {
            move_horz(SelFrame, SIZE_MAX, 1);
        } else if (c == 'a') {
            move_horz(SelFrame, 1, 1);
        } else if (c == 'I') {
            do_motion(SelFrame, MOTION_HOME_SP);
        }
        return UPDATE_UI | DO_RECORD;

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
        Core.dot_i = Core.user_recs[c - USER_REC_MIN].from;
        Core.dot_e = Core.user_recs[c - USER_REC_MIN].to;
        Core.repeat_count = Core.counter;
        return UPDATE_UI;

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

        default:
            motion = get_binded_motion(e_c);
            do_motion(SelFrame, motion);
            from = SelFrame->cur;
            to = cur;
            sort_positions(&from, &to);
            /* some motions do not want increment, this is to make some motions
             * seem more natural
             */
            switch (motion) {
            case MOTION_NEXT_WORD:
            case MOTION_PREV_WORD:
            case MOTION_UP:
            case MOTION_DOWN:
            case MOTION_LEFT:
            case MOTION_RIGHT:
            case MOTION_PREV:
            case MOTION_NEXT:
            case MOTION_FILE_BEG:
            case MOTION_FILE_END:
                break;

            default:
                to.col++;
            }
        }
        d_lines = get_lines(buf, &from, &to, &num_lines);
        yank_data(save_lines(d_lines, num_lines), 0);
        return UPDATE_UI;

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
            seg = load_undo_data(reg->data_i);
            if (seg == NULL) {
                return 0;
            }
            if ((reg->flags & IS_BLOCK)) {
                ev = insert_block(buf, &cur, seg->lines, seg->num_lines,
                        Core.counter);
            } else {
                ev = insert_lines(buf, &cur, seg->lines, seg->num_lines,
                        Core.counter);
            }
            unload_undo_data(seg);
        }
        ev->cur = SelFrame->cur;
        set_cursor(SelFrame, &ev->end);
        return UPDATE_UI;

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
        if (c < MARK_MIN || c > MARK_MAX) {
            set_error("invalid mark");
            return UPDATE_UI;
        }
        mark = &Core.marks[c - MARK_MIN];
        if (mark->buf == NULL) {
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

    case 'Z':
        init_file_list(&file_list, ".");
        if (get_deep_files(&file_list) == 0) {
            entry = choose_fuzzy((const char**) file_list.paths, file_list.num);
            if (entry != SIZE_MAX) {
                buf = create_buffer(file_list.paths[entry]);
                set_frame_buffer(SelFrame, buf);
            }
        }
        clear_file_list(&file_list);
        return UPDATE_UI;
    }
    return do_motion(SelFrame, get_binded_motion(c));
}
