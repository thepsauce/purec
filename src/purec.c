#include "color.h"
#include "frame.h"
#include "input.h"
#include "purec.h"
#include "xalloc.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

WINDOW *OffScreen;

struct core Core = {
    .rule = { 4, true },
    .theme = 44
};

static struct program_arguments {
    bool needs_help;
    bool load_session;
    char **files;
    int num_files;
} Args;

static const struct program_opt {
    const char *loong;
    const char *desc;
    char shrt;
    /* 0 - no arguments */
    /* 1 - single argument */
    /* 2 - optional argument */
    /* 3 - arguments until the next '-' */
    int n;
    bool *b; /* 0 | 2 */
    struct {
        char ***p;
        size_t *n;
    } v; /* 3 */
    char **s; /* 1 | 2 */
} ProgramOptions[] = {
    { "help", "show this help",
        'h', 0, .b = &Args.needs_help },
    { "load-session", "loads the last cached session",
        's', 0, .b = &Args.load_session },
};

void set_message(const char *msg, ...)
{
    va_list l;

    wclear(Core.msg_win);
    set_highlight(Core.msg_win, HI_CMD);
    va_start(l, msg);
    vw_printw(Core.msg_win, msg, l);
    va_end(l);
    Core.msg_state = MSG_OTHER;
}

void set_error(const char *err, ...)
{
    va_list l;

    wclear(Core.msg_win);
    set_highlight(Core.msg_win, HI_ERROR);
    va_start(l, err);
    vw_printw(Core.msg_win, err, l);
    va_end(l);
    Core.msg_state = MSG_OTHER;
}

int get_char(void)
{
    int             c;

    do {
        c = get_ch();
    } while (c == -1);
    return c;
}

struct mark *get_mark(struct frame *frame, char ch)
{
    static struct mark m;
    struct selection sel;

    if (ch >= 'A' && ch <= 'Z') {
        m = Core.marks[ch - 'A'];
    } else {
        switch (ch) {
        case '\'':
        case '`':
            m.buf = frame->buf;
            m.pos = frame->prev_cur;
            break;

        case '.':
        case '[':
        case ']':
            if (frame->buf->event_i == 0) {
                return NULL;
            }
            m.buf = frame->buf;
            if (ch != ']') {
                m.pos = frame->buf->events[frame->buf->event_i].pos;
            } else {
                m.pos = frame->buf->events[frame->buf->event_i].end;
            }
            break;

        case '^':
            m = Core.last_insert;
            break;

        case '<':
        case '>':
            (void) get_selection(&sel);
            m.buf = SelFrame->buf;
            m.pos = ch == '<' ? sel.beg : sel.end;
            break;

        default:
            return NULL;
        }
    }

    if (m.buf == NULL) {
        return NULL;
    }

    return &m;
}

void yank_data(struct undo_seg *seg, int flags)
{
    struct reg *reg;

    if (Core.user_reg == '+' || Core.user_reg == '*') {
        load_undo_data(seg);
        if (copy_clipboard(seg, Core.user_reg == '*') == -1) {
            unload_undo_data(seg);
        }
        /* else do not unload, copy will do that later */
    } else {
        reg = &Core.regs[Core.user_reg - '.'];
        reg->flags = flags;
        reg->seg = seg;
    }
}

col_t get_mode_line_end(const struct line *line)
{
    if (Core.mode != NORMAL_MODE) {
        return line->n;
    }
    /* adjustment for normal mode */
    return line->n == 0 ? 0 : move_back_glyph(line->s, line->n);
}

struct frame *get_frame_with_buffer(const struct buf *buf)
{
    struct frame    *frame;

    for (frame = FirstFrame; frame != NULL; frame = frame->next) {
        if (frame->buf == buf) {
            return frame;
        }
    }
    return NULL;
}

void set_mode(int mode)
{
    struct buf      *buf;
    col_t           indent;

    buf = SelFrame->buf;
    if (Core.mode == INSERT_MODE && buf->event_i > 0 &&
            (buf->events[buf->event_i - 1].flags & IS_AUTO_INDENT)) {
        (void) get_line_indent(buf, SelFrame->cur.line, &indent);
        if (indent == buf->text.lines[SelFrame->cur.line].n) {
            undo_event_no_trans(buf);
            if (buf->event_i > 0) {
                buf->events[buf->event_i - 1].flags |= IS_STOP;
            }
            SelFrame->cur.col = 0;
            (void) adjust_scroll(SelFrame);
        }
    }
    if (IS_VISUAL(mode) && !IS_VISUAL(Core.mode)) {
        Core.pos = SelFrame->cur;
    }

    Core.mode = mode;

    if (mode == NORMAL_MODE) {
        clip_column(SelFrame);
    } else if (mode == INSERT_MODE) {
        Core.ev_from_insert = SelFrame->buf->event_i;
        Core.repeat_insert = Core.counter;
    }

    Core.msg_state = MSG_TO_DEFAULT;
}

bool get_selection(struct selection *sel)
{
    if (Core.mode == VISUAL_BLOCK_MODE) {
        sel->is_block = true;
        sel->beg.line = MIN(SelFrame->cur.line, Core.pos.line);
        sel->beg.col = MIN(SelFrame->cur.col, Core.pos.col);
        sel->end.line = MAX(SelFrame->cur.line, Core.pos.line);
        sel->end.col = MAX(SelFrame->cur.col, Core.pos.col);
    } else {
        sel->is_block = false;
        sel->beg = SelFrame->cur;
        sel->end = Core.pos;
        sort_positions(&sel->beg, &sel->end);
    }
    return IS_VISUAL(Core.mode);
}

bool is_in_selection(const struct selection *sel, const struct pos *pos)
{
    if (sel->is_block) {
        return is_in_block(pos, &sel->beg, &sel->end);
    }
    return is_in_range(pos, &sel->beg, &sel->end);
}

void goto_fixit(int dir, size_t count)
{
    struct fixit    *next;
    struct frame    *frame;

    if (Core.num_fixits == 0) {
        set_message("no fix it items");
        return;
    }

    if (Core.num_fixits == 1) {
        Core.cur_fixit = 1;
    } else {
        count %= Core.num_fixits;
        if (dir > 0) {
            Core.cur_fixit += count;
            if (Core.cur_fixit >= Core.num_fixits) {
                Core.cur_fixit = 1;
            }
        } else {
            if (count >= Core.cur_fixit) {
                count -= Core.cur_fixit;
                Core.cur_fixit = Core.num_fixits - count;
            } else {
                Core.cur_fixit -= count;
            }
        }
    }
    next = &Core.fixits[Core.cur_fixit - 1];
    frame = get_frame_with_buffer(next->buf);
    if (frame == NULL) {
        set_frame_buffer(SelFrame, next->buf);
    } else {
        SelFrame = frame;
    }
    set_cursor(SelFrame, &next->pos);
    set_message("(%zu/%zu) %s\n", Core.cur_fixit, Core.num_fixits, next->msg);
}

static void usage(FILE *fp, const char *program_name)
{
    const struct program_opt *opt;

    fprintf(fp, "PureC text editor.\n"
            "%s [options] [files]\n"
            "options:\n", program_name);
    for (size_t i = 0; i < ARRAY_SIZE(ProgramOptions); i++) {
        opt = &ProgramOptions[i];
        if (opt->loong != NULL) {
            fprintf(fp, "--%s", opt->loong);
        }
        if (opt->shrt != '\0') {
            if (opt->loong != NULL) {
                fputc('|', fp);
            }
            fputc(opt->shrt, fp);
        }
        fprintf(fp, " - %s\n", opt->desc);
    }
}

static bool parse_args(int argc, char **argv)
{
    char *arg;
    char **vals;
    int num_vals;
    int on;
    const struct program_opt *o;
    char *equ;
    char s_arg[2];

    argc--;
    argv++;
    s_arg[1] = '\0';
    for (int i = 0; i != argc; ) {
        arg = argv[i];
        vals = NULL;
        num_vals = 0;
        on = -1;

        if (arg[0] == '-') {
            arg++;
            equ = strchr(arg, '=');
            if (equ != NULL) {
                *(equ++) = '\0';
            }
            if (arg[0] == '-' || equ != NULL) {
                if (arg[0] == '-') {
                    arg++;
                }

                if (arg[0] == '\0') {
                    i++;
                    Args.files = &argv[i];
                    Args.num_files = argc - i;
                    break;
                }

                for (size_t i = 0; i < ARRAY_SIZE(ProgramOptions); i++) {
                    if (strcmp(ProgramOptions[i].loong, arg) == 0) {
                        o = &ProgramOptions[i];
                        on = o->n;
                        break;
                    }
                }

                if (equ != NULL) {
                    argv[i] = equ;
                } else {
                    i++;
                }

                vals = &argv[i];
                if (on > 0 && i != argc) {
                    for (; i != argc && argv[i][0] != '-'; i++) {
                        num_vals++;
                        if (on != 1) {
                            break;
                        }
                    }
                } else if (equ != NULL) {
                    num_vals = 1;
                }
            } else {
                s_arg[0] = *(arg++);
                for (size_t i = 0; i < ARRAY_SIZE(ProgramOptions); i++) {
                    if (ProgramOptions[i].shrt == s_arg[0]) {
                        o = &ProgramOptions[i];
                        on = o->n;
                        break;
                    }
                }

                if (arg[0] != '\0') {
                    if (on > 0) {
                        argv[i] = arg;
                        vals = &argv[i];
                        num_vals = 1;
                        i++;
                    } else {
                        arg[-1] = '-';
                        argv[i] = arg - 1;
                    }
                } else {
                    i++;
                    if (on == 1 && i != argc) {
                        vals = &argv[i++];
                        num_vals = 1;
                    } else if (on == 3) {
                        vals = &argv[i];
                        for (; i != argc && argv[i][0] != '-'; i++) {
                            num_vals++;
                        }
                    }
                }
                arg = s_arg;
            }
        } else {
            /* use rest as files */
            Args.files = &argv[i];
            Args.num_files = argc - i;
            break;
        }

        switch (on) {
        case -1:
            fprintf(stderr, "invalid option '%s'\n", arg);
            return false;

        case 0:
            if (num_vals > 0) {
                fprintf(stderr, "option '%s' does not expect any arguments\n",
                        arg);
                return false;
            }
            *o->b = true;
            break;

        case 1:
            if (num_vals == 0) {
                fprintf(stderr, "option '%s' expects one argument\n", arg);
                return false;
            }
            *o->s = vals[0];
            break;

        case 2:
            *o->b = true;
            if (num_vals == 1) {
                *o->s = vals[0];
            }
            break;

        case 3:
            *o->v.p = vals;
            *o->v.n = num_vals;
            break;
        }
    }
    return true;
}

struct play_rec *get_playback(void)
{
    struct play_rec *rec;

    while (Core.rec_stack_n > 0) {
        rec = &Core.rec_stack[Core.rec_stack_n - 1];
        if (rec->index == rec->to) {
            if (rec->repeat_count > 0) {
                rec->repeat_count--;
                rec->index = rec->from;
            } else {
                Core.rec_stack_n--;
                continue;
            }
        }
        return rec;
    }
    return NULL;
}

void jump_to_file(const struct buf *buf, const struct pos *pos)
{
    struct line     *line;
    char            *s, *e;
    char            *path;
    size_t          i, l;
    struct stat     st;
    struct buf      *new_buf;
    struct frame    *frame;

    line = &buf->text.lines[pos->line];
    for (s = &line->s[pos->col]; s > line->s; s--) {
        if (!isalnum(s[-1]) &&
                s[-1] != '.' &&
                s[-1] != '/' &&
                s[-1] != '_' &&
                s[-1] != '-') {
            break;
        }
    }
    for (e = &line->s[pos->col]; e < &line->s[line->n]; e++) {
        if (!isalnum(e[0]) &&
                e[0] != '.' &&
                e[0] != '/' &&
                e[0] != '_' &&
                e[0] != '-') {
            break;
        }
    }
    if (e == s) {
        set_error("there is no file at the cursor");
        return;
    }

    path = xmalloc(strlen(buf->path) + e - s + 1);
    for (i = 0, l = 0; buf->path[i] != '\0'; i++) {
        if (buf->path[i] == '/') {
            l = i + 1;
        }
        path[i] = buf->path[i];
    }
    memcpy(&path[l], s, e - s);
    path[l + e - s] = '\0';
    if (stat(path, &st) == 0) {
        goto found;
    }

    path = xrealloc(path, STRING_SIZE("/usr/include/") + e - s + 1);
    memcpy(path, "/usr/include/", STRING_SIZE("/usr/include/"));
    memcpy(&path[STRING_SIZE("/usr/include/")], s, e - s);
    path[STRING_SIZE("/usr/include/") + e - s] = '\0';
    if (stat(path, &st) == 0) {
        goto found;
    }

    free(path);

    set_error("could not find file '%.*s'\n", (int) (e - s), s);
    return;

found:
    new_buf = create_buffer(path);
    frame = get_frame_with_buffer(new_buf);
    if (frame == NULL) {
        set_frame_buffer(SelFrame, new_buf);
    } else {
        SelFrame = frame;
    }
    free(path);
}

int load_last_session(void)
{
    DIR             *dir;
    struct dirent   *ent;
    struct stat     st;
    char            *name;
    char            *latest_name;
    time_t          latest_time;
    FILE            *fp;

    dir = opendir(Core.session_dir);
    if (dir == NULL) {
        return -1;
    }

    latest_name = NULL;
    latest_time = 0;
    while (ent = readdir(dir), ent != NULL) {
        if (ent->d_type != DT_REG) {
            continue;
        }
        name = xasprintf("%s/%s", Core.session_dir, ent->d_name);
        if (stat(name, &st) != 0) {
            continue;
        }
        if (st.st_mtime > latest_time) {
            free(latest_name);
            latest_name = name;
            latest_time = st.st_mtime;
        } else {
            free(name);
        }
    }

    closedir(dir);

    fp = fopen(latest_name, "rb");
    (void) load_session(fp);
    if (fp != NULL) {
        fclose(fp);
    }

    free(latest_name);
    return 0;
}

int handle_input(int c)
{
   static int (*const input_handlers[])(int c) = {
        [NORMAL_MODE] = normal_handle_input,
        [INSERT_MODE] = insert_handle_input,
        [VISUAL_MODE] = visual_handle_input,
        [VISUAL_BLOCK_MODE] = visual_handle_input,
        [VISUAL_LINE_MODE] = visual_handle_input,
    };
    return input_handlers[Core.mode](c);
}

static void sigint_handler(int sig)
{
    (void) sig;
    Core.num_sigs++;
    if (Core.child_pid != 0) {
        kill(Core.child_pid, SIGKILL);
        Core.child_pid = 0;
    } else if (!Core.is_busy) {
        handle_input(CONTROL('C'));
        render_all();
        refresh();
    }
}

int init_purec(int argc, char **argv)
{
    const char      *home;

    setlocale(LC_ALL, "");

    if (!parse_args(argc, argv) ||  Args.needs_help) {
        usage(stderr, argv[0]);
        return 0;
    }

    home = getenv("HOME");
    if (home == NULL) {
        fprintf(stderr, "could not get environment variable 'HOME': %s\n",
                strerror(errno));
        return -1;
    }

    Core.cache_dir = xasprintf("%s/.cache/purec", home);
    if (mkdir(Core.cache_dir, 0755) == -1 && errno != EEXIST) {
        fprintf(stderr, "could not create, '%s/.cache/purec': %s\n",
                home, strerror(errno));
        return -1;
    }

    Core.session_dir = xasprintf("%s/sessions", Core.cache_dir);
    if (mkdir(Core.session_dir, 0755) == -1 && errno != EEXIST) {
        Core.session_dir = xstrdup("/tmp");
        fprintf(stderr, "could not create session directory,"
                        "using /tmp instead\n");
        /* do not return */
    }

    initscr();
    cbreak();
    keypad(stdscr, true);
    noecho();

    set_escdelay(0);

    signal(SIGINT, sigint_handler);

    init_colors();
    init_clipboard();

    Core.msg_win = newpad(1, 128);
    Core.preview_win = newpad(1, 128);
    OffScreen = newpad(1, 512);
    
    if (Core.msg_win == NULL || Core.preview_win == NULL || OffScreen == NULL) {
        endwin();
        fprintf(stderr, "failed creating ncurses windows\n");
        return -1;
    }

    wbkgdset(stdscr, ' ' | COLOR_PAIR(HI_NORMAL));
    wbkgdset(Core.msg_win, ' ' | COLOR_PAIR(HI_NORMAL));
    wbkgdset(Core.preview_win, ' ' | COLOR_PAIR(HI_NORMAL));
    wbkgdset(OffScreen, ' ' | COLOR_PAIR(HI_NORMAL));

    for (int i = 0; i < Args.num_files; i++) {
        (void) create_buffer(Args.files[i]);
    }

    if ((!Args.load_session || load_last_session() == -1) &&
            Args.num_files == 0) {
        /* stored in `FirstBuffer` */
        (void) create_buffer(NULL);
    }

    if (FirstFrame == NULL) {
        (void) create_frame(NULL, 0, FirstBuffer);
    }

    if (SelFrame == NULL) {
        SelFrame = FirstFrame;
    }

    set_mode(NORMAL_MODE);
    return 0;
}

static void set_message_to_default(void)
{
    static const char *mode_strings[] = {
        [NORMAL_MODE]       = "",

        [INSERT_MODE]       = "-- INSERT --",

        [VISUAL_MODE]       = "-- VISUAL --",
        [VISUAL_LINE_MODE]  = "-- VISUAL LINE --",
        [VISUAL_BLOCK_MODE] = "-- VISUAL BLOCK --",
    };

    set_highlight(Core.msg_win, HI_CMD);
    wattr_on(Core.msg_win, A_BOLD, NULL);
    mvwaddstr(Core.msg_win, 0, 0, mode_strings[Core.mode]);
    if (Core.mode == INSERT_MODE && Core.counter > 1) {
        wprintw(Core.msg_win, " REPEAT * %zu", Core.counter);
    }
    wattr_off(Core.msg_win, A_BOLD, NULL);

    if (Core.user_rec_ch != '\0') {
        if (getcurx(Core.msg_win) > 0) {
            waddch(Core.msg_win, ' ');
        }
        waddstr(Core.msg_win, "recording @");
        waddch(Core.msg_win, tolower(Core.user_rec_ch));
    }
    wclrtoeol(Core.msg_win);
}

void render_all(void)
{
    static const char cursors[] = {
        [NORMAL_MODE]       = '\x30',

        [INSERT_MODE]       = '\x35',

        [VISUAL_MODE]       = '\x30',
        [VISUAL_LINE_MODE]  = '\x30',
        [VISUAL_BLOCK_MODE] = '\x30',
    };

    int             cur_x, cur_y;
    struct frame    *frame;
    int             x;
    size_t          d, d2;
    
    curs_set(0);

    erase();

    for (frame = FirstFrame; frame != NULL; frame = frame->next) {
        render_frame(frame);
    }

    if (Core.msg_state == MSG_TO_DEFAULT) {
        set_message_to_default();
        Core.msg_state = MSG_DEFAULT;
    }
    copywin(Core.msg_win, stdscr, 0, 0, LINES - 1, 0, LINES - 1,
            MIN(COLS - 1, getmaxx(Core.msg_win) - 1), 0);

    set_highlight(stdscr, HI_CMD);
    if (Core.mode == VISUAL_MODE) {
        if (Core.pos.line == SelFrame->cur.line) {
            d = Core.pos.col > SelFrame->cur.col ?
                    Core.pos.col - SelFrame->cur.col :
                    SelFrame->cur.col - Core.pos.col;
            mvprintw(LINES - 1, COLS * 3 / 4, "%zu", d + 1);
        } else {
            d = Core.pos.line > SelFrame->cur.line ?
                    Core.pos.line - SelFrame->cur.line :
                    SelFrame->cur.line - Core.pos.line;
            mvprintw(LINES - 1, COLS * 3 / 4, "%zu", d + 1);
        }
    } else if (Core.mode == VISUAL_LINE_MODE) {
        d = Core.pos.line > SelFrame->cur.line ?
                Core.pos.line - SelFrame->cur.line :
                SelFrame->cur.line - Core.pos.line;
        mvprintw(LINES - 1, COLS * 3 / 4, "%zu", d + 1);
    } else if (Core.mode == VISUAL_BLOCK_MODE) {
        d = Core.pos.line > SelFrame->cur.line ?
                Core.pos.line - SelFrame->cur.line :
                SelFrame->cur.line - Core.pos.line;
        d2 = Core.pos.col > SelFrame->cur.col ?
                Core.pos.col - SelFrame->cur.col :
                SelFrame->cur.col - Core.pos.col;
        mvprintw(LINES - 1, COLS * 3 / 4, "%zux%zu", d + 1, d2 + 1);
    }

    x = getcurx(Core.preview_win);
    if (x > 0) {
        x -= COLS / 4;
        x = MAX(x, 0);
        copywin(Core.preview_win, stdscr,
                0, x, LINES - 1, COLS * 3 / 4,
                LINES - 1, MIN(COLS - 1, getmaxx(Core.preview_win) - 1), 0);
    }

    if (get_visual_pos(SelFrame, &SelFrame->cur, &cur_x, &cur_y)) {
        move(cur_y, cur_x);
        printf("\x1b[%c q", cursors[Core.mode]);
        fflush(stdout);
        curs_set(1);
    }
}

int leave_purec(void)
{
    /* restore terminal state */
    endwin();

    /* instantly free the result */
    free(save_current_session());

    /* free resources */
    free_session();
    return Core.exit_code;
}
