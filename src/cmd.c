#include "xalloc.h"
#include "buf.h"
#include "cmd.h"
#include "util.h"
#include "frame.h"
#include "mode.h"

#include <ctype.h>
#include <ncurses.h>
#include <string.h>

struct command_line CmdLine;

/**
 * Renders the command line at the end of the file prepended by ':'.
 */
static void render_command_line(void)
{
    move(LINES - 1, 0);
    attr_on(A_REVERSE, NULL);
    addch(':');
    attr_off(A_REVERSE, NULL);
    addnstr(&CmdLine.buf[CmdLine.scroll],
            MIN((size_t) (COLS - 1), CmdLine.len - CmdLine.scroll));
    clrtoeol();
    move(LINES - 1, 1 + CmdLine.index - CmdLine.scroll);
}

static int run_command(const char *cmd)
{
    char c;
    bool f = false;

    cmd += strspn(cmd, " \t");
    if (cmd[0] == '\0') {
        return 0;
    }

    if (!isalpha(cmd[0])) {
        format_message("expected word");
        return -1;
    }

    c = cmd[0];
    cmd++;
    if (cmd[0] == 'q') {
        if (c == 'w') {
            c = 'x';
        }
    }
    if (cmd[0] == '!') {
        f = true;
        cmd++;
    }

    cmd += strspn(cmd, " \t");
    switch (c) {
    case 'w':
        return write_file(SelFrame->buf, cmd[0] == '\0' ?
                (f ? SelFrame->buf->path : NULL) : cmd);

    case 'r':
        read_file(SelFrame->buf, &SelFrame->cur, cmd);
        break;

    case 'x':
        if (write_file(SelFrame->buf, cmd[0] == '\0' ?
                        (f ? SelFrame->buf->path : NULL) : cmd) != 0 && !f) {
            break;
        }
        IsRunning = false;
        break;

    case 'q':
        if (!f && SelFrame->buf->save_event_i != SelFrame->buf->event_i) {
            format_message("buffer has changed, use :q! to quit");
            return 0;
        }
        IsRunning = false;
        break;

    default:
        format_message("what is that command?");
        return -1;
    }
    return 0;
}

void read_command_line(void)
{
    int c;
    char ch;
    char *h;
    char *now;
    size_t now_len = 0;
    size_t index;

    CmdLine.index = 0;
    CmdLine.len = 0;
    CmdLine.history_index = CmdLine.num_history;
    while (render_command_line(), c = getch(), c != '\n' && c != '\x1b') {
        switch (c) {
        case KEY_HOME:
            CmdLine.index = 0;
            break;

        case KEY_END:
            CmdLine.index = CmdLine.len;
            break;

        case KEY_LEFT:
            if (CmdLine.index == 0) {
                break;
            }
            CmdLine.index--;
            break;

        case KEY_RIGHT:
            if (CmdLine.index == CmdLine.len) {
                break;
            }
            CmdLine.index++;
            break;

        case KEY_UP:
            if (CmdLine.history_index == 0) {
                break;
            }

            for (index = CmdLine.history_index; index > 0; index--) {
                h = CmdLine.history[index - 1];
                if (strncmp(h, CmdLine.buf, CmdLine.len) == 0) {
                    break;
                }
            }

            if (CmdLine.history_index == CmdLine.num_history) {
                now = xmemdup(CmdLine.buf, CmdLine.len);
                now_len = CmdLine.len;
            }
            h = CmdLine.history[--CmdLine.history_index];
            CmdLine.len = strlen(h);
            memcpy(CmdLine.buf, h, CmdLine.len);
            break;

        case KEY_DOWN:
            if (CmdLine.history_index == CmdLine.num_history) {
                break;
            }

            if (CmdLine.history_index + 1 == CmdLine.num_history) {
                memcpy(CmdLine.buf, now, now_len);
                CmdLine.len = now_len;
                free(now);
                CmdLine.history_index++;
            } else {
                h = CmdLine.history[++CmdLine.history_index];
                CmdLine.len = strlen(h);
                memcpy(CmdLine.buf, h, CmdLine.len);
            }
            break;

        case KEY_BACKSPACE:
        case '\x7f':
            if (CmdLine.index == 0) {
                break;
            }
            memmove(&CmdLine.buf[CmdLine.index - 1],
                    &CmdLine.buf[CmdLine.index],
                    CmdLine.len - CmdLine.index);
            CmdLine.len--;
            CmdLine.index--;
            break;

        case KEY_DC:
            if (CmdLine.index == CmdLine.len) {
                break;
            }
            CmdLine.len--;
            memmove(&CmdLine.buf[CmdLine.index],
                    &CmdLine.buf[CmdLine.index + 1],
                    CmdLine.len - CmdLine.index);
            break;

        default:
            ch = c;
            if (c >= 0x100 || ch < ' ') {
                break;
            }
            if (CmdLine.len == CmdLine.a) {
                CmdLine.a *= 2;
                CmdLine.a++;
                CmdLine.buf = xrealloc(CmdLine.buf, CmdLine.a);
            }
            memmove(&CmdLine.buf[CmdLine.index + 1],
                    &CmdLine.buf[CmdLine.index], CmdLine.len - CmdLine.index);
            CmdLine.buf[CmdLine.index++] = ch;
            CmdLine.len++;
        }
        if (CmdLine.index < CmdLine.scroll) {
            CmdLine.scroll = CmdLine.index;
            if (CmdLine.scroll <= 5) {
                CmdLine.scroll = 0;
            } else {
                CmdLine.scroll -= 5;
            }
        } else if (CmdLine.index >= CmdLine.scroll + COLS - 1) {
            CmdLine.scroll = CmdLine.index - COLS + 7;
        }
    }

    move(LINES - 1, 0);
    clrtoeol();

    if (c == '\x1b' || CmdLine.len == 0) {
        return;
    }

    /* add command to history */
    CmdLine.history = xreallocarray(CmdLine.history, CmdLine.num_history + 1,
            sizeof(*CmdLine.history));
    CmdLine.history[CmdLine.num_history++] =
        xstrndup(CmdLine.buf, CmdLine.len);

    /* add null terminator */
    if (CmdLine.len == CmdLine.a) {
        CmdLine.a *= 2;
        CmdLine.a++;
        CmdLine.buf = xrealloc(CmdLine.buf, CmdLine.a);
    }
    CmdLine.buf[CmdLine.len] = '\0';

    run_command(CmdLine.buf);
}
