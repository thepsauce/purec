#include "buf.h"
#include "cmd.h"
#include "frame.h"
#include "fuzzy.h"
#include "purec.h"

#include <ctype.h>
#include <ncurses.h>

int ConvChar;

int conv_to_char(int c)
{
    (void) c;
    return ConvChar;
}

int normal_handle_input(int c)
{
    static int motions[KEY_MAX] = {
        [KEY_LEFT] = MOTION_LEFT,
        [KEY_RIGHT] = MOTION_RIGHT,
        [KEY_UP] = MOTION_UP,
        [KEY_DOWN] = MOTION_DOWN,

        ['h'] = MOTION_LEFT,
        ['l'] = MOTION_RIGHT,
        ['k'] = MOTION_UP,
        ['j'] = MOTION_DOWN,

        ['0'] = MOTION_HOME,
        ['$'] = MOTION_END,

        ['H'] = MOTION_BEG_FRAME,
        ['M'] = MOTION_MIDDLE_FRAME,
        ['L'] = MOTION_END_FRAME,

        ['f'] = MOTION_FIND_NEXT,
        ['F'] = MOTION_FIND_PREV,

        ['t'] = MOTION_FIND_EXCL_NEXT,
        ['T'] = MOTION_FIND_EXCL_PREV,

        ['W'] = MOTION_NEXT_WORD,
        ['w'] = MOTION_NEXT_WORD,

        ['E'] = MOTION_END_WORD,
        ['e'] = MOTION_END_WORD,

        ['B'] = MOTION_PREV_WORD,
        ['b'] = MOTION_PREV_WORD,

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
    struct pos cur, from, to;
    int e_c;
    struct undo_event *ev;
    struct raw_line lines[2];
    struct frame *frame;
    struct file_list file_list;
    size_t entry;
    int next_mode = NORMAL_MODE;

    buf = SelFrame->buf;
    switch (c) {
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
            to.line = safe_add(from.line, Core.counter);
            to.col = SIZE_MAX;
            ev = delete_range(buf, &from, &to);
            indent_line(buf, from.line);
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
            do_motion(SelFrame, motions[e_c]);
            from = SelFrame->cur;
            to = cur;
            sort_positions(&from, &to);
            /* some motions do not want increment, this is to make some motions
             * seem more natural
             */
            switch (motions[e_c]) {
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
            ev->undo_cur = cur;
            ev->redo_cur = SelFrame->cur;
            r = UPDATE_UI;
        }
        r |= DO_RECORD;
        set_mode(next_mode);
        break;

    case 'x':
    case 's':
        cur = SelFrame->cur;
        cur.col = safe_add(cur.col, Core.counter);
        ev = delete_range(buf, &SelFrame->cur, &cur);
        if (ev != NULL) {
            ev->undo_cur = SelFrame->cur;
            ev->redo_cur = SelFrame->cur;
            r = UPDATE_UI;
        }
        if (c == 's') {
            Core.counter = 0;
            set_mode(INSERT_MODE);
            r = UPDATE_UI;
        } else {
            clip_column(SelFrame);
        }
        r |= DO_RECORD;
        break;

    case 'X':
        cur = SelFrame->cur;
        (void) move_horz(SelFrame, Core.counter, -1);
        ev = delete_range(buf, &cur, &SelFrame->cur);
        if (ev != NULL) {
            ev->undo_cur = cur;
            ev->redo_cur = SelFrame->cur;
            r = UPDATE_UI | DO_RECORD;
        }
        break;

    case 'r':
        c = get_ch();
        if (!isprint(c) && c != '\n') {
            break;
        }
        cur = SelFrame->cur;
        cur.col = safe_add(cur.col, Core.counter);
        if (c == '\n') {
            ev = delete_range(buf, &SelFrame->cur, &cur);
            if (ev != NULL) {
                ev->undo_cur = SelFrame->cur;
            }
            ev = break_line(buf, &SelFrame->cur);
        } else {
            ConvChar = c;
            ev = change_range(buf, &SelFrame->cur, &cur, conv_to_char);
            ev->undo_cur = SelFrame->cur;
        }
        ev->redo_cur = ev->end;
        set_cursor(SelFrame, &cur);
        return UPDATE_UI | DO_RECORD;

    case 'u':
        for (size_t i = 0; i < Core.counter; i++) {
            ev = undo_event(buf);
            if (ev == NULL) {
                return 0;
            }
        }
        set_cursor(SelFrame, &ev->undo_cur);
        return UPDATE_UI;

    case CONTROL('R'):
        for (size_t i = 0; i < Core.counter; i++) {
            ev = redo_event(buf);
            if (ev == NULL) {
                return 0;
            }
        }
        set_cursor(SelFrame, &ev->redo_cur);
        return UPDATE_UI;

    case 'O':
        set_mode(INSERT_MODE);
        cur = SelFrame->cur;
        if (cur.line == 0) {
            lines[0].n = 0;
            lines[1].n = 0;
            ev = insert_lines(buf, &cur, lines, 2, 1);
            ev->undo_cur = cur;
            clip_column(SelFrame);
            ev->redo_cur = SelFrame->cur;
        } else {
            cur.line--;
            cur.col = buf->lines[cur.line].n;
            ev = break_line(buf, &cur);
            ev->undo_cur = SelFrame->cur;
            set_cursor(SelFrame, &SelFrame->cur);
            ev->redo_cur = SelFrame->cur;
        }
        return UPDATE_UI | DO_RECORD;

    case 'o':
        set_mode(INSERT_MODE);
        cur.line = SelFrame->cur.line;
        cur.col = buf->lines[cur.line].n;
        ev = break_line(buf, &cur);
        ev->undo_cur = SelFrame->cur;
        set_cursor(SelFrame, &ev->end);
        ev->redo_cur = ev->end;
        return UPDATE_UI | DO_RECORD;

    case ':':
        read_command_line(":");
        return UPDATE_UI;

    case CONTROL('C'):
        werase(Message);
        waddstr(Message, "Type  :qa!<ENTER>  to quit and abandon all changes");
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
                frame = frame_at(SelFrame->x - 1, SelFrame->y);
            }
            break;

        case CONTROL('J'):
        case 'j':
            for (frame = SelFrame; Core.counter > 0; Core.counter--) {
                frame = frame_at(frame->x, frame->y + frame->h);
            }
            break;

        case CONTROL('K'):
        case 'k':
            for (frame = SelFrame; Core.counter > 0; Core.counter--) {
                frame = frame_at(SelFrame->x, SelFrame->y - 1);
            }
            break;

        case CONTROL('L'):
        case 'l':
            for (frame = SelFrame; Core.counter > 0; Core.counter--) {
                frame = frame_at(SelFrame->x + SelFrame->w, SelFrame->y);
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
        if (c == 'a') {
            move_horz(SelFrame, 1, 1);
        }
        return UPDATE_UI | DO_RECORD;

    case 'q':
        if (Core.user_rec_ch != '\0') {
            /* minus 1 to exclude the 'q' */
            Core.user_recs[Core.user_rec_ch - 'a'].to = Core.rec_len - 1;
            Core.user_rec_ch = '\0';
            return UPDATE_UI;
        }
        c = get_ch();
        if (c < 'a' || c > 'z') {
            break;
        }
        Core.user_rec_ch = c;
        Core.user_recs[Core.user_rec_ch - 'a'].from = Core.rec_len;
        return UPDATE_UI;

    case '@':
        c = get_ch();
        if (c < 'a' || c > 'z') {
            break;
        }
        Core.dot_i = Core.user_recs[c - 'a'].from;
        Core.dot_e = Core.user_recs[c - 'a'].to;
        Core.repeat_count = Core.counter;
        return UPDATE_UI;

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
    return r | do_motion(SelFrame, motions[c]);
}
