#include "buf.h"
#include "cmd.h"
#include "color.h"
#include "frame.h"
#include "fuzzy.h"
#include "input.h"
#include "lang.h"
#include "purec.h"
#include "xalloc.h"

#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <string.h>
#include <limits.h>

#define ACCEPTS_RANGE   0x1
#define ACCEPTS_NUMBER  0x2

struct cmd_data {
    char *arg;
    size_t from, to;
    bool has_number, has_range;
    bool force;
};

#include "cmd_impl.h"

#define MAX_COMMAND_NAME 12

static const struct cmd {
    /// name of the command
    char name[MAX_COMMAND_NAME];
    /// whether this command accepts a range or a number
    int flags;
    /// callback of the command to call
    int (*callback)(struct cmd_data *cd);
} Commands[] = {
    { "b", ACCEPTS_NUMBER, cmd_buffer },

    { "bn", ACCEPTS_NUMBER, cmd_bnext },
    { "bnext", ACCEPTS_NUMBER, cmd_bnext },

    { "bp", ACCEPTS_NUMBER, cmd_bprev },
    { "bprev", ACCEPTS_NUMBER, cmd_bprev },

    { "buffer", ACCEPTS_NUMBER, cmd_buffer },

    { "colo", 0, cmd_colorscheme },
    { "colorscheme", 0, cmd_colorscheme },

    { "cq", ACCEPTS_NUMBER, cmd_cquit },
    { "cquit", ACCEPTS_NUMBER, cmd_cquit },

    { "e", 0, cmd_edit },
    { "edit", 0, cmd_edit },

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

    { "syn", 0, cmd_syntax },
    { "syntax", 0, cmd_syntax },

    { "w", ACCEPTS_RANGE, cmd_write },
    { "wa", 0, cmd_write_all },
    { "wall", 0, cmd_write_all },

    { "wq", ACCEPTS_RANGE, cmd_exit },
    { "wqa", 0, cmd_exit_all },
    { "wqall", 0, cmd_exit_all },

    { "wquit", 0, cmd_exit },

    { "write", ACCEPTS_RANGE, cmd_write },

    { "x", 0, cmd_exit },
    { "xa", 0, cmd_exit_all },
    { "xall", 0, cmd_exit_all },
    { "xit", 0, cmd_exit },
};

static const struct cmd *get_command(const char *s, size_t s_m)
{
    struct {
        const struct cmd *cmd;
        int val;
    } mins[3];
    int min_cnt = 0;
    const char *s0;
    int v0[MAX_COMMAND_NAME];
    int v1[MAX_COMMAND_NAME];
    int m, n;
    int del_cost, ins_cost, sub_cost;
    int d;
    int max, max_i;

    if (s_m >= 12) {
        set_error("command '%.*s' does not exist", (int) s_m, s);
        return NULL;
    }

    m = s_m;
    for (size_t c = 0; c < ARRAY_SIZE(Commands); c++) {
        s0 = Commands[c].name;
        n = strlen(s0);

        for (int i = 0; i <= n; i++) {
            v0[i] = i;
        }

        for (int i = 0; i < m; i++) {
            v1[0] = i + 1;

            for (int j = 0; j < n; j++) {
                del_cost = v0[j + 1] + 1;
                ins_cost = v1[j] + 1;
                sub_cost = v0[j];
                if (s[i] != s0[j]) {
                    sub_cost++;
                }
                v1[j + 1] = MIN(del_cost, MIN(ins_cost, sub_cost));
            }

            memcpy(v0, v1, sizeof(v1));
        }

        d = v0[n];
        if (d == 0) {
            return &Commands[c];
        }
        if (d < m) {
            if (min_cnt < (int) ARRAY_SIZE(mins)) {
                mins[min_cnt].cmd = &Commands[c];
                mins[min_cnt].val = d;
                min_cnt++;
            } else {
                max = mins[0].val;
                max_i = 0;
                for (int i = 1; i < min_cnt; i++) {
                    if (mins[i].val > max) {
                        max = mins[i].val;
                        max_i = i;
                    }
                }
                if (d < max) {
                    mins[max_i].cmd = &Commands[c];
                    mins[max_i].val = d;
                }
            }
        }
    }

    set_error("command '%.*s' does not exist", (int) m, s);
    if (min_cnt > 0) {
        waddstr(Core.msg_win, ", did you mean");
        for (int i = 0; i < min_cnt; i++) {
            if (i > 0) {
                if (i + 1 == min_cnt) {
                    waddstr(Core.msg_win, " or");
                } else {
                    waddch(Core.msg_win, ',');
                }
            }
            wprintw(Core.msg_win, " '%s'", mins[i].cmd->name);
        }
        waddch(Core.msg_win, '?');
    }
    return NULL;
}

static bool read_number(char *s, char **p_s, size_t *p_n)
{
    struct mark *mark;

    if (s[0] == '\'' || s[0] == '`') {
        s++;
        mark = get_mark(SelFrame, s[0]);
        if (mark == NULL) {
            set_error("invalid mark: %c", s[0]);
            return -1;
        }
        *p_n = mark->pos.line;
        *p_s = s + 1;
        return 0;
    }
    if (!isdigit(s[0])) {
        return 1;
    }
    *p_n = strtoull(s, p_s, 10);
    return 0;
}

static int run_command(char *s_cmd)
{
    int r;
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
        r = read_number(s_cmd, &s_cmd, &data.from);
        if (r == -1) {
            return -1;
        }

        data.has_number = r == 0;
        if (s_cmd[0] == ',') {
            s_cmd++;
            data.has_range = true;

            if (!data.has_number) {
                data.from = 0;
            }
            r = read_number(s_cmd, &s_cmd, &data.to);
            if (r == -1) {
                return -1;
            }
            if (r != 0) {
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
        set_error("expected word but got: '%.*s'", 8, s_cmd);
        return -1;
    }

    cmd = get_command(s_cmd, len);
    if (cmd == NULL) {
        return -1;
    }
    if (cmd->callback == NULL) {
        set_error("'%s' is not implemented", cmd->name);
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
            set_error("'%s' does not expect a range", cmd->name);
            return -1;
        }
        if (data.has_number && !(cmd->flags & ACCEPTS_NUMBER)) {
            set_error("'%s' does not expect a number", cmd->name);
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

const struct cmd *get_command_beg(const char *beg, size_t i, size_t n)
{
    size_t prev_i;

    if (n >= MAX_COMMAND_NAME) {
        return NULL;
    }
    do {
        prev_i = i;
        for (; i < ARRAY_SIZE(Commands); i++) {
            if (memcmp(Commands[i].name, beg, n) == 0) {
                if (strlen(Commands[i].name) == n) {
                    continue;
                }
                return &Commands[i];
            }
        }
        i = 0;
    } while (prev_i != 0);
    return NULL;
}

/// tab complete data
struct tab_complete {
    /// current paths
    glob_t g;
    /// index of next path
    size_t i;
    /// tab completion is active
    bool active;
} Tab;

void tab_complete(int dir)
{
    size_t i;
    size_t st;
    const struct cmd *cmd;
    size_t cur_len, len;

    i = 1;
    while (i < Input.n && isblank(Input.s[i])) {
        i++;
    }
    if (Input.index < i) {
        /* either there is no command or the cursor is before it */
        return;
    }
    st = i;

    while (i < Input.index && isalpha(Input.s[i])) {
        i++;
    }

    if (i != Input.index) {
        /* this means the cursor is not at the first word */
        while (i < Input.index && isblank(Input.s[i])) {
            i++;
        }
        if (!Tab.active) {
            if (Input.n == Input.a) {
                Input.a *= 2;
                Input.s = xrealloc(Input.s, Input.a);
            }
            Input.s[Input.n++] = '*';
            terminate_input();
            if (glob(&Input.s[i], 0, NULL, &Tab.g) != 0) {
                /* remove the trailing '*' again */
                Input.n--;
                return;
            }
            Tab.i = dir == -1 ? Tab.g.gl_pathc - 1 : 0;
            Tab.active = true;
        } else if (dir == -1) {
            if (Tab.i == 0) {
                Tab.i = Tab.g.gl_pathc - 1;
            } else {
                Tab.i--;
            }
        } else {
            Tab.i++;
            Tab.i %= Tab.g.gl_pathc;
        }
        cur_len = Input.n - i;
        len = strlen(Tab.g.gl_pathv[Tab.i]);
        if (Input.n + len - cur_len > Input.a) {
            Input.a += len - cur_len;
            Input.s = xrealloc(Input.s, Input.a);
        }
        memmove(&Input.s[i + cur_len],
                &Input.s[i],
                Input.n - i - cur_len);
        memcpy(&Input.s[i], Tab.g.gl_pathv[Tab.i], len);
        Input.n += len - cur_len;
        Input.index = Input.n;
        return;
    }
    /* auto complete command */
    /* :  ed\t /home/steves */
    cur_len = i - st;
    cmd = get_command_beg(&Input.s[st], 0, cur_len);
    if (cmd == NULL) {
        return;
    }
    len = strlen(cmd->name);
    if (Input.n + len - cur_len > Input.a) {
        Input.a += len - cur_len;
        Input.s = xrealloc(Input.s, Input.a);
    }
    memmove(&Input.s[st + cur_len],
            &Input.s[st],
            Input.n - st - cur_len);
    memcpy(&Input.s[st], cmd->name, len);
    Input.n += len - cur_len;
    Input.index += len - cur_len;
}

void read_command_line(const char *beg)
{
    static char **history = NULL;
    static size_t num_history = 0;

    int c;
    char *s;

    Core.msg_state = MSG_TO_DEFAULT;

    set_input_text(beg, 1);
    set_input_history(history, num_history);
    do {
        Input.x = 0;
        Input.y = LINES - 1;
        Input.max_w = COLS;
        set_highlight(stdscr, HI_CMD);
        render_input();
        c = get_ch();
        if (c == '\t' || c == KEY_BTAB) {
            tab_complete(c == '\t' ? 1 : -1);
        } else {
            if (Tab.active) {
                globfree(&Tab.g);
            }
            Tab.active = false;
            s = send_to_input(c);
        }
    } while (s == NULL);

    if (s[0] == '\n') {
        return;
    }

    history = xreallocarray(history, num_history + 1, sizeof(*history));
    history[num_history++] = xstrdup(&s[-Input.prefix]);

    switch (beg[0]) {
    case '/':
    case '?':
        (void) search_string(SelFrame->buf, s);
        Core.counter = 1;
        (void) do_motion(SelFrame, beg[0] == '/' ? 'n' : 'p');
        break;

    case ':':
        run_command(s);
        break;
    }
}
