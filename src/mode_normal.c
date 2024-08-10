#include "cmd.h"
#include "fuzzy.h"
#include "frame.h"
#include "buf.h"
#include "mode.h"
#include "util.h"

#include <ctype.h>
#include <ncurses.h>

int normal_handle_input(int c)
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

        ['G'] = MOTION_FILE_END,
        ['g'] = MOTION_FILE_BEG,

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

    buf = SelFrame->buf;
    switch (c) {
    case 'g':
        Mode.extra_counter = Mode.counter;
        Mode.counter = 0;
        e_c = getch_digit();
        Mode.counter = safe_mul(correct_counter(Mode.counter),
                correct_counter(Mode.extra_counter));
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
        c = 'D';
        /* fall through */
    case 'c':
        set_mode(INSERT_MODE);
        /* fall through */
    case 'd':
        Mode.extra_counter = Mode.counter;
        Mode.counter = 0;
        e_c = getch_digit();
        Mode.counter = safe_mul(correct_counter(Mode.counter),
                correct_counter(Mode.extra_counter));

        /* fall through */
    case 'D':
    case 'S':
        if (c == 'D') {
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
            to.line = safe_add(from.line, correct_counter(Mode.counter) - 1);
            to.col = SIZE_MAX;
            ev = delete_range(buf, &from, &to);
            ev->flags |= IS_TRANSIENT;
            indent_line(buf, from.line);
            from.col = buf->lines[from.line].n;
            set_cursor(SelFrame, &from);
            break;

        case 'd':
            if (c != 'd') {
                break;
            }

            to.line = safe_add(cur.line, correct_counter(Mode.counter));
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
                /* just go up once */
                Mode.counter = 0;
                do_motion(SelFrame, MOTION_UP);
            } else {
                clip_column(SelFrame);
            }
            break;

        default:
            Mode.type = INSERT_MODE;
            do_motion(SelFrame, motions[e_c]);
            Mode.type = NORMAL_MODE;
            ev = delete_range(buf, &cur, &SelFrame->cur);
            if (cur.line < SelFrame->cur.line ||
                    (cur.line == SelFrame->cur.line &&
                     cur.col < SelFrame->cur.col)) {
                SelFrame->cur = cur;
            }
            /* setting it to itself to clip it */
            set_cursor(SelFrame, &SelFrame->cur);
        }
        if (ev != NULL) {
            ev->undo_cur = cur;
            ev->redo_cur = SelFrame->cur;
            r = 1;
        }
        break;

    case 'x':
    case 's':
        cur = SelFrame->cur;
        cur.col = safe_add(cur.col, correct_counter(Mode.counter));
        ev = delete_range(buf, &SelFrame->cur, &cur);
        if (ev != NULL) {
            ev->undo_cur = SelFrame->cur;
            ev->redo_cur = SelFrame->cur;
            r = 1;
        }
        if (c == 's') {
            set_mode(INSERT_MODE);
            r = 1;
        }
        break;

    case 'X':
        cur = SelFrame->cur;
        move_horz(SelFrame, correct_counter(Mode.counter), -1);
        ev = delete_range(buf, &cur, &SelFrame->cur);
        if (ev != NULL) {
            ev->undo_cur = cur;
            ev->redo_cur = SelFrame->cur;
            r = 1;
        }
        break;

    case 'u':
        for (size_t i = 0; i < correct_counter(Mode.counter); i++) {
            ev = undo_event(buf);
            if (ev == NULL) {
                return 0;
            }
        }
        set_cursor(SelFrame, &ev->undo_cur);
        return 1;

    case CONTROL('R'):
        for (size_t i = 0; i < correct_counter(Mode.counter); i++) {
            ev = redo_event(buf);
            if (ev == NULL) {
                return 0;
            }
        }
        set_cursor(SelFrame, &ev->redo_cur);
        return 1;

    case 'O':
        cur = SelFrame->cur;
        set_mode(INSERT_MODE);
        do_motion(SelFrame, MOTION_HOME);
        lines[0].n = 0;
        lines[1].n = 0;
        ev = insert_lines(buf, &SelFrame->cur, lines, 2, 1);
        ev->undo_cur = cur;
        ev->flags |= IS_TRANSIENT;
        indent_line(buf, SelFrame->cur.line);
        do_motion(SelFrame, MOTION_END);
        ev->redo_cur = SelFrame->cur;
        return 1;

    case 'o':
        cur = SelFrame->cur;
        set_mode(INSERT_MODE);
        do_motion(SelFrame, MOTION_END);
        lines[0].n = 0;
        lines[1].n = 0;
        ev = insert_lines(buf, &SelFrame->cur, lines, 2, 1);
        ev->undo_cur = cur;
        ev->flags |= IS_TRANSIENT;
        indent_line(buf, SelFrame->cur.line + 1);
        do_motion(SelFrame, MOTION_DOWN);
        ev->redo_cur = SelFrame->cur;
        return 1;

    case ':':
        read_command_line(":");
        return 1;

    case CONTROL('C'):
        format_message("Type  :qa!<ENTER>  to quit and abandon all changes");
        return 1;

    case 'v':
        set_mode(VISUAL_MODE);
        return 1;

    case 'V':
        set_mode(VISUAL_LINE_MODE);
        return 1;

    case CONTROL('W'):
        Mode.extra_counter = Mode.counter;
        Mode.counter = 0;
        e_c = getch_digit();
        Mode.counter = safe_mul(correct_counter(Mode.counter),
                correct_counter(Mode.extra_counter));

        frame = SelFrame;
        switch (e_c) {
        case CONTROL('Q'):
        case 'q':
            destroy_frame(SelFrame);
            frame = SelFrame;
            break;

        /* moving to next entries or previous entries in the focus chain is safe
         * but not jumping to frames that have never been focused before
         */
        case CONTROL('P'):
        case 'p':
            for (frame = FirstFrame; frame->next_focus != SelFrame; ) {
                frame = frame->next_focus;
            }
            SelFrame = frame;
            break;

        case CONTROL('N'):
        case 'n':
            SelFrame = SelFrame->next_focus;
            frame = SelFrame;
            break;

        case CONTROL('H'):
        case 'h':
            frame = frame_at(SelFrame->x - 1, SelFrame->y);
            break;

        case CONTROL('J'):
        case 'j':
            frame = frame_at(SelFrame->x, SelFrame->y + SelFrame->h);
            break;

        case CONTROL('K'):
        case 'k':
            frame = frame_at(SelFrame->x, SelFrame->y - 1);
            break;

        case CONTROL('L'):
        case 'l':
            frame = frame_at(SelFrame->x + SelFrame->w, SelFrame->y);
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
        if (frame != NULL && frame != SelFrame) {
            (void) focus_frame(frame);
        }
        return 1;

    case CONTROL('V'):
        set_mode(VISUAL_BLOCK_MODE);
        return 1;

    case 'A':
    case 'a':
    case 'I':
    case 'i':
        Mode.repeat_count = correct_counter(Mode.counter);
        set_mode(INSERT_MODE);
        r = 1;
        break;

    case 'Z':
        init_file_list(&file_list, ".");
        if (get_deep_files(&file_list) == 0) {
            entry = choose_fuzzy((const char**) file_list.paths, file_list.num);
            if (entry == SIZE_MAX) {
                format_message("[Nothing]");
            } else {
                buf = create_buffer(file_list.paths[entry]);
                set_frame_buffer(SelFrame, buf);
            }
        }
        clear_file_list(&file_list);
        return 1;
    }
    return r | do_motion(SelFrame, motions[c]);
}
