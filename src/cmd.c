#include "buf.h"
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
    line_t from, to;
    bool has_number, has_range;
    bool force;
};

struct input Cmd;

#include "cmd_impl.h"

#define MAX_COMMAND_NAME 12

#define TAB_COMMAND     1
#define TAB_PATH        2
#define TAB_COLOR       3
#define TAB_SYNTAX      4
#define TAB_HIGHLIGHT   5

static const struct cmd {
    /// name of the command
    char name[MAX_COMMAND_NAME];
    /// whether this command accepts a range or a number
    int flags;
    /// callback of the command to call
    int (*callback)(struct cmd_data *cd);
    /// the tab completion to use
    int tab_rule;
} Commands[] = {
    { "b", ACCEPTS_NUMBER, cmd_buffer, 0 },

    { "bn", ACCEPTS_NUMBER, cmd_bnext, 0 },
    { "bnext", ACCEPTS_NUMBER, cmd_bnext, 0 },

    { "bp", ACCEPTS_NUMBER, cmd_bprev, 0 },
    { "bprev", ACCEPTS_NUMBER, cmd_bprev, 0 },

    { "buffer", ACCEPTS_NUMBER, cmd_buffer, 0 },

    { "colo", 0, cmd_colorscheme, TAB_COLOR },
    { "coloi", 0, cmd_colorschemeindex, TAB_COLOR },
    { "coloindex", 0, cmd_colorschemeindex, TAB_COLOR },
    { "colorscheme", 0, cmd_colorscheme, TAB_COLOR },

    { "cq", ACCEPTS_NUMBER, cmd_cquit, 0 },
    { "cquit", ACCEPTS_NUMBER, cmd_cquit, 0 },

    { "e", 0, cmd_edit, TAB_PATH },
    { "edit", 0, cmd_edit, TAB_PATH },

    { "exi", 0, cmd_exit, 0 },
    { "exit", 0, cmd_exit, 0 },
    { "exita", 0, cmd_exit_all, 0 },
    { "exitall", 0, cmd_exit_all, 0 },

    { "hi", 0, cmd_highlight, TAB_HIGHLIGHT },
    { "highlight", 0, cmd_highlight, TAB_HIGHLIGHT },

    { "noh", 0, cmd_nohighlight, 0 },
    { "nohighlight", 0, cmd_nohighlight, 0 },

    { "q", 0, cmd_quit, 0 },
    { "qa", 0, cmd_quit_all, 0 },
    { "qall", 0, cmd_quit_all, 0 },

    { "quit", 0, cmd_quit, 0 },
    { "quita", 0, cmd_quit_all, 0 },
    { "quitall", 0, cmd_quit_all, 0 },

    { "r", 0, cmd_read, TAB_PATH },
    { "read", 0, cmd_read, TAB_PATH },

    { "s", ACCEPTS_RANGE, cmd_substitute, 0 },
    { "substitute", ACCEPTS_RANGE, cmd_substitute, 0 },

    { "syn", 0, cmd_syntax, TAB_SYNTAX },
    { "syntax", 0, cmd_syntax, TAB_SYNTAX },

    { "w", ACCEPTS_RANGE, cmd_write, TAB_PATH },
    { "wa", 0, cmd_write_all, 0 },
    { "wall", 0, cmd_write_all, 0 },

    { "wq", ACCEPTS_RANGE, cmd_exit, 0 },
    { "wqa", 0, cmd_exit_all, 0 },
    { "wqall", 0, cmd_exit_all, 0 },

    { "wquit", 0, cmd_exit, 0 },

    { "write", ACCEPTS_RANGE, cmd_write, TAB_PATH },

    { "x", 0, cmd_exit, 0 },
    { "xa", 0, cmd_exit_all, 0 },
    { "xall", 0, cmd_exit_all, 0 },
    { "xit", 0, cmd_exit, 0 },
};

/**
 * Gets the command with given name or NULL when it does not exist.
 *
 * @param s     The name of the command.
 * @param s_m   The length of `s`.
 *
 * @return The command with that name.
 */
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

    if (s_m >= MAX_COMMAND_NAME) {
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

static bool read_number(char *s, char **p_s, line_t *p_n)
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

int run_command(char *s_cmd)
{
    int                 r;
    size_t              len;
    const struct cmd    *cmd;
    struct cmd_data     data;
    FILE                *pp;
    struct buf          *buf;
    line_t              i;

    s_cmd += strspn(s_cmd, " \t");
    if (s_cmd[0] == '\0') {
        return 0;
    }

    if (s_cmd[0] == '%') {
        data.has_range = true;
        data.from = 0;
        data.to = LINE_MAX;
        s_cmd++;
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
                data.to = LINE_MAX;
            }
        } else {
            data.has_range = false;
        }
    }

    if (s_cmd[0] == '!') {
        s_cmd++;

        endwin();

        pp = popen(s_cmd, "w");
        if (pp == NULL) {
            set_error("could not open process");
            return -1;
        }

        buf = SelFrame->buf;
        data.to = MIN(data.to, buf->num_lines - 1);
        for (i = data.from; i <= data.to; i++) {
            fwrite(buf->lines[i].s, buf->lines[i].n, 1, pp);
            fputc('\n', pp);
        }
        pclose(pp);

        printf("\nPress enter to return...\n");
        while (getch() != '\n') {
            (void) 0;
        }
        return 0;
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
                data.to = LINE_MAX;
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

/// tab complete data
static struct tab_complete {
    /// the type of completion
    int type;
    /// the paths
    glob_t g;
    /// index of next item
    int item_i;
    /// the range to replace with the next tab completion
    size_t from, to;
    /// the last pattern
    char *pat;
} Tab;

int get_command_beg(const char *beg, size_t n)
{
    if (n >= MAX_COMMAND_NAME) {
        return -1;
    }
    for (int i = 0; i < (int) ARRAY_SIZE(Commands); i++) {
        if (strncmp(Commands[i].name, beg, n) == 0) {
            return i;
        }
    }
    return -1;
}

static void replace_completion(const char *item)
{
    size_t len, cur_len;

    cur_len = Tab.to - Tab.from;
    len = strlen(item);
    if (Cmd.n + len - cur_len > Cmd.a) {
        Cmd.a += len - cur_len;
        Cmd.s = xrealloc(Cmd.s, Cmd.a);
    }
    memmove(&Cmd.s[Tab.from + cur_len],
            &Cmd.s[Tab.from],
            Cmd.n - Tab.to);
    memcpy(&Cmd.s[Tab.from], item, len);
    Cmd.n += len - cur_len;
    Cmd.index = Cmd.n;
    switch (Tab.type) {
    case TAB_COMMAND:
        Tab.to += len - cur_len;
        break;

    default:
        Tab.to = Cmd.n;
    }
}

static void do_completion(int dir)
{
    size_t len;
    int i;
    int prev_i;
    int num;
    const char *name;

    switch (Tab.type) {
    case TAB_COMMAND:
    case TAB_COLOR:
    case TAB_SYNTAX:
    case TAB_HIGHLIGHT:
        len = strlen(Tab.pat);
        i = Tab.item_i;
        num = Tab.type == TAB_COMMAND ?
                (int) ARRAY_SIZE(Commands) : Tab.type == TAB_SYNTAX ?
                (int) ARRAY_SIZE(Langs) : Tab.type == TAB_COLOR ?
                get_number_of_themes() : HI_MAX;
        do {
            prev_i = i;
            for (i += dir; i >= 0 && i < num; i += dir) {
                name = Tab.type == TAB_COMMAND ?
                        Commands[i].name : Tab.type == TAB_SYNTAX ?
                        Langs[i].name : Tab.type == TAB_COLOR ?
                        Themes[i].name : HiNames[i];
                if (strncasecmp(name, Tab.pat, len) == 0) {
                    replace_completion(name);
                    Tab.item_i = i;
                    return;
                }
            }
            if (i < 0) {
                /* implies `dir = -1` */
                i = num;
            } else if (i > 0) {
                /* implies `dir = 1` */
                i = -1;
            }
        } while (prev_i != i);
        break;

    case TAB_PATH:
        if (Tab.g.gl_pathc == 0) {
            break;
        }
        if (dir == -1) {
            if (Tab.item_i == 0) {
                Tab.item_i = Tab.g.gl_pathc - 1;
            } else {
                Tab.item_i--;
            }
        } else {
            Tab.item_i++;
            Tab.item_i %= Tab.g.gl_pathc;
        }
        replace_completion(Tab.g.gl_pathv[Tab.item_i]);
        break;
    }
}

static void tab_complete(int dir)
{
    size_t i;
    size_t st;
    int c_i;

    if (Tab.type != 0) {
        do_completion(dir);
        return;
    }

    i = 1;
    while (i < Cmd.n && isblank(Cmd.s[i])) {
        i++;
    }
    if (Cmd.index < i) {
        /* the cursor is before the command it */
        return;
    }
    st = i;

    while (i < Cmd.index && isalpha(Cmd.s[i])) {
        i++;
    }

    Tab.item_i = -1;
    if (i != Cmd.index) {
        /* this means the cursor is not at the first word */

        c_i = get_command_beg(&Cmd.s[st], i - st);
        if (c_i == -1) {
            Tab.type = TAB_PATH;
        } else {
            Tab.type = Commands[c_i].tab_rule;
        }
        while (i < Cmd.index && isblank(Cmd.s[i])) {
            i++;
        }
        if (Tab.type == TAB_PATH) {
            if (Cmd.n == Cmd.a) {
                Cmd.a *= 2;
                Cmd.s = xrealloc(Cmd.s, Cmd.a);
            }
            Cmd.s[Cmd.n++] = '*';
            terminate_input(&Cmd);
            if (glob(&Cmd.s[i], 0, NULL, &Tab.g) != 0) {
                /* remove the trailing '*' again */
                Cmd.n--;
                return;
            }
            /* do not need pattern here */
            Tab.pat = NULL;
        } else {
            Tab.pat = xstrndup(&Cmd.s[i], Cmd.n - i);
        }
        Tab.from = i;
        Tab.to = Cmd.n;
    } else {
        /* auto complete command */
        Tab.type = TAB_COMMAND;
        Tab.from = st;
        Tab.to = i;
        Tab.pat = xstrndup(&Cmd.s[st], i - st);
    }
    do_completion(dir);
}

void read_command_line(const char *beg)
{
    static char **history = NULL;
    static size_t num_history = 0;

    int             c;
    struct pos      cur, scroll;
    int             r;

    Core.msg_state = MSG_TO_DEFAULT;

    set_input_text(&Cmd, beg, 1);
    set_input_history(&Cmd, history, num_history);
    cur = SelFrame->cur;
    scroll = SelFrame->scroll;
    do {
        Cmd.x = 0;
        Cmd.y = LINES - 1;
        Cmd.max_w = COLS;
        set_highlight(stdscr, HI_CMD);
        render_input(&Cmd);
        c = get_ch();
        if (c == '\t' || c == KEY_BTAB) {
            tab_complete(c == '\t' ? 1 : -1);
            r = INP_CHANGED;
        } else {
            if (Tab.type == TAB_PATH) {
                globfree(&Tab.g);
            }
            free(Tab.pat);
            Tab.pat = NULL;
            Tab.type = 0;
            r = send_to_input(&Cmd, c);
            if (r == INP_CHANGED && (beg[0] == '/' || beg[0] == '?')) {
                /* get live results */
                terminate_input(&Cmd);
                (void) search_pattern(SelFrame->buf, &Cmd.s[Cmd.prefix]);
                SelFrame->cur = cur;
                SelFrame->scroll = scroll;
                Core.counter = 1;
                if (beg[0] == '?') {
                    Core.search_dir = -1;
                } else {
                    Core.search_dir = 1;
                }
                (void) do_motion(SelFrame, 'n');
                render_all();
            }
        }
    } while (r > INP_FINISHED);

    SelFrame->cur = cur;
    SelFrame->scroll = scroll;

    if (r == INP_CANCELLED || Cmd.n == Cmd.prefix) {
        return;
    }

    history = xreallocarray(history, num_history + 1, sizeof(*history));
    history[num_history++] = xstrdup(Cmd.s);

    switch (beg[0]) {
    case '/':
    case '?':
        (void) do_motion(SelFrame, 'n');
        break;

    case ':':
        run_command(&Cmd.s[Cmd.prefix]);
        break;
    }
}
