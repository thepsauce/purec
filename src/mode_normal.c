#include "frame.h"
#include "buf.h"
#include "mode.h"

#include <ctype.h>
#include <ncurses.h>

size_t safe_mul(size_t a, size_t b)
{
    size_t c;

    if (__builtin_mul_overflow(a, b, &c)) {
        return SIZE_MAX;
    }
    return c;
}

#define correct(c) ((c)==0?1:(c))

int normal_handle_input(int c)
{
    int r = 0;
    struct pos old_cur;
    struct line *line;
    size_t col;

    old_cur = SelFrame->cur;
    switch (c) {
    case '\x1b':
        move_horz(SelFrame, 1, -1);
        set_mode(NORMAL_MODE);
        r = 1;
        break;

    /* go to change or delete extra mode */
    case 'c':
    case 'd':
        if (Mode.extra != 0) {
            /* TODO: change indents */
            r = delete_lines(SelFrame->buf, SelFrame->cur.line,
                    safe_mul(correct(Mode.extra_counter),
                        correct(Mode.counter)));
            adjust_cursor(SelFrame);
            Mode.extra = 0;
            break;
        }
        Mode.pos = SelFrame->cur;
        Mode.extra_counter = Mode.counter;
        Mode.counter = 0;
        Mode.extra = c == 'c' ? EXTRA_CHANGE : EXTRA_DELETE;
        return 0;

    case 'I':
        line = &SelFrame->buf->lines[SelFrame->cur.line];
        col = 0;
        while (isblank(line->s[col])) {
            col++;
        }
        r = move_horz(SelFrame, SelFrame->cur.col - col, -1);
        set_mode(INSERT_MODE);
        break;

    case KEY_HOME:
    case '0':
        if (Mode.counter != 0) {
            shift_add_counter(0);
            return 1;
        }
        r = move_horz(SelFrame, SIZE_MAX, -1);
        break;

    case 'x':
        r = delete_range(SelFrame->buf, &SelFrame->cur, &SelFrame->cur);
        break;

    case 'X':
        r = move_horz(SelFrame, 1, -1);
        delete_range(SelFrame->buf, &SelFrame->cur, &old_cur);
        break;

    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        shift_add_counter(c - '0');
        return 1;

    case 'A':
        set_mode(INSERT_MODE);
        /* fall through */
    case KEY_END:
    case '$':
        r = move_horz(SelFrame, SIZE_MAX, 1);
        break;

    case KEY_LEFT:
    case 'h':
        r = move_horz(SelFrame, Mode.counter == 0 ? 1 : Mode.counter, -1);
        break;

    case KEY_DOWN:
    case 'j':
        r = move_vert(SelFrame, Mode.counter == 0 ? 1 : Mode.counter, 1);
        break;

    case KEY_UP:
    case 'k':
        r = move_vert(SelFrame, Mode.counter == 0 ? 1 : Mode.counter, -1);
        break;

    case KEY_BACKSPACE:
    case 0x7f:
        r = move_dir(SelFrame, Mode.counter == 0 ? 1 : Mode.counter, -1);
        break;

    case ' ':
        r = move_dir(SelFrame, Mode.counter == 0 ? 1 : Mode.counter, 1);
        break;

    case 'a':
        set_mode(INSERT_MODE);
        /* fall through */
    case KEY_RIGHT:
    case 'l':
        r = move_horz(SelFrame, Mode.counter == 0 ? 1 : Mode.counter, 1);
        break;

    case 'i':
        set_mode(INSERT_MODE);
        break;
    }
    Mode.counter = 0;
    return r;
}
