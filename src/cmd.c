#include "buf.h"
#include "cmd.h"
#include "frame.h"
#include "input.h"
#include "mode.h"
#include "util.h"
#include "xalloc.h"

#include <ctype.h>
#include <errno.h>
#include <ncurses.h>
#include <string.h>

#define ACCEPTS_RANGE   0x1
#define ACCEPTS_NUMBER  0x2

struct cmd_data {
    char *arg;
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

static bool read_number(char *s, char **p_s, size_t *p_n)
{
    if (s[0] == '\'') {
        /* TODO: get mark */
        s++;
        *p_n = SIZE_MAX;
        *p_s = s + 1;
        return true;
    }
    if (!isdigit(s[0])) {
        return false;
    }
    *p_n = strtoull(s, p_s, 10);
    return true;
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

    if (s_cmd[0] == '%') {
        data.has_range = true;
        data.from = 0;
        data.to = SIZE_MAX;
    } else {
        data.has_number = read_number(s_cmd, &s_cmd, &data.from);

        if (s_cmd[0] == ',') {
            s_cmd++;
            data.has_range = true;

            if (!data.has_number) {
                data.from = 0;
            }
            if (!read_number(s_cmd, &s_cmd, &data.to)) {
                data.to = SIZE_MAX;
            }
        } else {
            data.has_range = false;
        }
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
                data.to = SIZE_MAX;
            } else {
                data.to = data.from;
            }
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
    static char **history = NULL;
    static size_t num_history = 0;

    char *s;

    free(Message);
    Message = NULL;

    set_input(0, LINES - 1, COLS, beg, 1, history, num_history);

    while (render_input(), s = send_to_input(getch()), s == NULL) {
        (void) 0;
    }

    if (s[0] == '\n') {
        return;
    }

    history = xreallocarray(history, num_history + 1, sizeof(*history));
    history[num_history++] = xstrdup(&s[-Input.prefix]);

    switch (beg[0]) {
    case '?':
    case '/':
        /* TODO: */
        break;

    case ':':
        run_command(s);
        break;
    }
}
