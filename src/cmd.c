#include "xalloc.h"
#include "buf.h"
#include "cmd.h"
#include "util.h"
#include "frame.h"
#include "mode.h"

#include <ctype.h>
#include <errno.h>
#include <ncurses.h>
#include <string.h>

struct command_line CmdLine;

#define ACCEPTS_RANGE   0x1
#define ACCEPTS_NUMBER  0x2

struct cmd_data {
    const char *arg;
    size_t from, to;
    bool has_number, has_range;
    bool force;
};

#include "cmd_impl.h"

static const struct cmd {
    /// name of the command
    const char *name;
    /// whether this command accepts a range
    int flags;
    /// callback of the command to call
    int (*callback)(struct cmd_data *cd);
} Commands[] = {
    /* must be sorted */
    { "cq", ACCEPTS_NUMBER, cmd_cquit },
    { "cquit", ACCEPTS_NUMBER, cmd_cquit },

    { "exi", 0, cmd_exit },
    { "exit", 0, cmd_exit },
    { "exita", 0, cmd_exit_all },
    { "exitall", 0, cmd_exit_all },

    { "q", 0, cmd_quit },
    { "qa", 0, cmd_quit_all },
    { "qall", 0, cmd_quit_all },

    { "quit", 0, cmd_quit },
    { "quita", 0, cmd_quit_all },
    { "quitall", 0, cmd_quit_all },

    { "r", 0, cmd_read },
    { "read", 0, cmd_read },

    { "w", ACCEPTS_RANGE, cmd_write },
    { "wa", 0, cmd_write_all },
    { "wall", 0, cmd_write_all },

    { "wq", ACCEPTS_RANGE, cmd_exit },
    { "wqa", 0, cmd_exit_all },
    { "wqall", 0, cmd_exit_all },

    { "wquit", 0, cmd_exit },

    { "write", ACCEPTS_RANGE, cmd_write },

    { "x", 0, cmd_exit },
    { "xit", 0, cmd_exit },
    { "xa", 0, cmd_exit_all },
    { "xall", 0, cmd_exit_all },
};

static const struct cmd *get_command(const char *s, size_t s_len)
{
    size_t l, m, r;
    const char *name;
    int cmp;

    l = 0;
    r = ARRAY_SIZE(Commands);
    while (l < r) {
        m = (l + r) / 2;
        name = Commands[m].name;
        cmp = strncmp(name, s, s_len);
        if (cmp == 0) {
            if (name[s_len] == '\0') {
                return &Commands[m];
            }
            cmp = (unsigned char) name[s_len];
        }

        if (cmp < 0) {
            l = m + 1;
        } else {
            r = m;
        }
    }

    format_message("command '%.*s' does not exist, did you mean '%s'?",
            (int) s_len, s, Commands[l].name);
    return NULL;
}

/**
 * Renders the command line at the end of the screen.
 */
static void render_command_line(void)
{
    move(LINES - 1, 0);
    addnstr(&CmdLine.buf[CmdLine.scroll],
            MIN((size_t) COLS, CmdLine.len - CmdLine.scroll));
    clrtoeol();
    move(LINES - 1, CmdLine.index - CmdLine.scroll);
}

static int run_command(char *s_cmd)
{
    size_t len;
    const struct cmd *cmd;
    struct cmd_data data;

    s_cmd += strspn(s_cmd, " \t");
    if (s_cmd[0] == '\0') {
        return 0;
    }

    if (isdigit(s_cmd[0])) {
        data.from = strtoull(s_cmd, &s_cmd, 10);
        data.has_number = true;
    } else {
        data.has_number = false;
    }

    if (s_cmd[0] == ',') {
        s_cmd++;
        data.has_range = true;

        if (!data.has_number) {
            data.from = 0;
        }

        if (isdigit(s_cmd[0])) {
            data.to = strtoull(s_cmd, &s_cmd, 10);
        } else {
            data.to = SIZE_MAX;
        }
    } else {
        data.has_range = false;
    }

    for (len = 0; isalpha(s_cmd[len]); ) {
        len++;
    }
    if (len == 0) {
        format_message("expected word but got: '%.*s'", 8, s_cmd);
        return -1;
    }

    cmd = get_command(s_cmd, len);
    if (cmd == NULL) {
        return -1;
    }
    if (cmd->callback == NULL) {
        format_message("'%s' is not implemented", cmd->name);
        return -1;
    }

    if ((cmd->flags & ACCEPTS_RANGE)) {
        if (!data.has_range) {
            if (!data.has_number) {
                data.from = 0;
            }
            data.to = SIZE_MAX;
        }
    } else {
        if (data.has_range) {
            format_message("'%s' does not expect a range", cmd->name);
            return -1;
        }
        if (data.has_number && !(cmd->flags & ACCEPTS_NUMBER)) {
            format_message("'%s' does not expect a number", cmd->name);
            return -1;
        }
    }

    s_cmd += len;

    if (s_cmd[0] == '!') {
        data.force = true;
        s_cmd++;
    } else {
        data.force = false;
    }

    s_cmd += strspn(s_cmd, " \t");
    data.arg = s_cmd;
    cmd->callback(&data);
    return 0;
}

void read_command_line(const char *beg)
{
    int c;
    char ch;
    char *h;
    char *now;
    size_t now_len = 0;
    size_t index;


    free(Message);
    Message = NULL;

    CmdLine.len = strlen(beg);
    CmdLine.index = CmdLine.len;
    if (CmdLine.a < CmdLine.len) {
        CmdLine.a = CmdLine.len + 128;
        CmdLine.buf = xrealloc(CmdLine.buf, CmdLine.a);
    }
    memcpy(CmdLine.buf, beg, CmdLine.len);

    CmdLine.history_index = CmdLine.num_history;
    while (render_command_line(), c = getch(), c != '\n' && c != '\x1b') {
        switch (c) {
        case KEY_HOME:
            CmdLine.index = 1;
            break;

        case KEY_END:
            CmdLine.index = CmdLine.len;
            break;

        case KEY_LEFT:
            if (CmdLine.index == 1) {
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
            if (CmdLine.index == 1) {
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
        } else if (CmdLine.index >= CmdLine.scroll + COLS) {
            CmdLine.scroll = CmdLine.index - COLS + 6;
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

    switch (CmdLine.buf[0]) {
    case '?':
    case '/':
        /* TODO: */
        break;

    case ':':
        run_command(&CmdLine.buf[1]);
        break;
    }
}
