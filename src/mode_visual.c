#include "cmd.h"
#include "frame.h"
#include "buf.h"
#include "mode.h"
#include "util.h"

#include <ctype.h>
#include <ncurses.h>

int visual_handle_input(int c)
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

        ['g'] = MOTION_FILE_BEG,
        ['G'] = MOTION_FILE_END,

        [0x7f] = MOTION_PREV,
        [KEY_BACKSPACE] = MOTION_PREV,
        [' '] = MOTION_NEXT,

        [KEY_PPAGE] = MOTION_PAGE_UP,
        [KEY_NPAGE] = MOTION_PAGE_DOWN,
    };

    int r = 0;
    struct buf *buf;
    struct pos cur;
    int e_c;

    buf = SelFrame->buf;
    switch (c) {
    case 'g':
        Mode.extra_counter = Mode.counter;
        Mode.counter = 0;
        e_c = getch_digit();
        switch (e_c) {
        case 'g':
        case 'G':
            c = e_c;
            break;
        default:
            e_c = 0;
        }
        break;

    /* change or delete */
    case 'C':
    case 'c':
    case 'S':
    case 's':
        set_mode(INSERT_MODE);
        /* fall through */
    case 'D':
    case 'd':
    case 'X':
    case 'x':
        break;

    case 'U':
    case 'u':
        return 1;

    case 'O':
    case 'o':
        /* swap the cursor */
        cur = SelFrame->cur;
        SelFrame->cur = Mode.pos;
        Mode.pos = cur;
        return 1;

    case ':':
        read_command_line();
        return 1;

    case '{':
        for (size_t i = SelFrame->cur.line; i > 0; ) {
            i--;
            if (buf->lines[i].n == 0) {
                if (Mode.counter > 1) {
                    Mode.counter--;
                    continue;
                }
                Mode.counter = SelFrame->cur.line - i;
                return do_motion(SelFrame, MOTION_UP);
            }
        }
        Mode.counter = SelFrame->cur.line;
        return do_motion(SelFrame, MOTION_UP);

    case '}':
        for (size_t i = SelFrame->cur.line + 1; i < buf->num_lines; i++) {
            if (buf->lines[i].n == 0) {
                if (Mode.counter > 1) {
                    Mode.counter--;
                    continue;
                }
                Mode.counter = i - SelFrame->cur.line;
                return do_motion(SelFrame, MOTION_DOWN);
            }
        }
        Mode.counter = buf->num_lines - 1 - SelFrame->cur.line;
        return do_motion(SelFrame, MOTION_DOWN);

    case 'v':
    case 'V':
    case CONTROL('V'):
        set_mode(NORMAL_MODE);
        return 1;

    case 'A':
    case 'I':
        set_mode(INSERT_MODE);
        return 1;
    }
    if (r == 0) {
        return do_motion(SelFrame, motions[c]);
    }
    return r;
}
