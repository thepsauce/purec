#include "color.h"
#include "frame.h"
#include "input.h"
#include "purec.h"
#include "xalloc.h"

#include <locale.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

WINDOW *OffScreen;

struct core Core;

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

void yank_data(struct undo_seg *seg, int flags)
{
    struct reg *reg;

    if (Core.user_reg == '+' || Core.user_reg == '*') {
        load_undo_data(seg);
        copy_clipboard(seg->data, seg->data_len, Core.user_reg == '*');
        unload_undo_data(seg);
    } else {
        reg = &Core.regs[Core.user_reg - '.'];
        reg->flags = flags;
        reg->seg = seg;
    }
}

bool is_playback(void)
{
    return Core.play_i < Core.dot_e || Core.repeat_count > 0;
}

int get_ch(void)
{
    int c;

    if (Core.play_i >= Core.dot_e) {
        if (Core.repeat_count > 0) {
            Core.repeat_count--;
            Core.play_i = Core.dot_i;
        } else {
            Core.play_i = SIZE_MAX;
        }
    }

    if (Core.play_i != SIZE_MAX) {
        c = Core.rec[Core.play_i++];
        if ((c & 0xf0) == 0xf0) {
            c = (c & 0x1) << 8;
            c |= Core.rec[Core.play_i++];
        }
    } else {
        do {
            c = getch();
            if (c == KEY_RESIZE) {
                update_screen_size();
            }
        } while (c == KEY_RESIZE);
        if (c > 0xff) {
            if (Core.rec_len + 2 > Core.a_rec) {
                Core.a_rec *= 2;
                Core.a_rec += 2;
                Core.rec = xrealloc(Core.rec, Core.a_rec);
            }
            Core.rec[Core.rec_len++] = 0xf0 | ((c & 0x100) >> 8);
            Core.rec[Core.rec_len++] = c & 0xff;
        } else {
            if (Core.rec_len + 1 > Core.a_rec) {
                Core.a_rec *= 2;
                Core.a_rec++;
                Core.rec = xrealloc(Core.rec, Core.a_rec);
            }
            Core.rec[Core.rec_len++] = c;
        }
    }
    return c;
}

static int get_first_char(void)
{
    int c;
    size_t counter = 0, new_counter;

    Core.counter = 1;
    Core.user_reg = '.';
    while (c = get_ch(), (c == '0' && counter != 0) || (c >= '1' && c <= '9') ||
            (c == '\"')) {
        if (c == '\"') {
            if (counter != 0) {
                Core.counter = counter;
                counter = 0;
            }
            c = toupper(get_ch());
            if (!IS_REG_CHAR(c)) {
                return -1;
            }
            Core.user_reg = c;
        } else {
            /* add a digit to the counter */
            new_counter = counter * 10 + c - '0';
            /* check for overflow */
            if (new_counter < counter) {
                new_counter = SIZE_MAX;
            }
            counter = new_counter;
        }
    }

    if (counter == 0) {
        return c;
    }

    Core.counter = safe_mul(Core.counter, counter);
    return c;
}

int get_extra_char(void)
{
    int c;
    size_t counter = 0, new_counter;

    while (c = get_ch(), (c == '0' && counter != 0) || (c >= '1' && c <= '9')) {
        /* add a digit to the counter */
        new_counter = counter * 10 + c - '0';
        /* check for overflow */
        if (new_counter < counter) {
            new_counter = SIZE_MAX;
        }
        counter = new_counter;
    }

    if (counter == 0) {
        return c;
    }

    Core.counter = safe_mul(Core.counter, counter);
    return c;
}

size_t get_mode_line_end(struct line *line)
{
    if (Core.mode != NORMAL_MODE) {
        return line->n;
    }
    /* adjustment for normal mode */
    return line->n == 0 ? 0 : line->n - 1;
}

void set_mode(int mode)
{
    if (IS_VISUAL(mode) && !IS_VISUAL(Core.mode)) {
        Core.pos = SelFrame->cur;
    }

    Core.mode = mode;

    if (mode == NORMAL_MODE) {
        clip_column(SelFrame);
    } else if (mode == INSERT_MODE) {
        Core.ev_from_ins = SelFrame->buf->event_i;
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
        if (Core.mode == VISUAL_LINE_MODE) {
            sel->beg.col = 0;
            sel->end.col = 0;
            sel->end.line++;
        }
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

static void set_message_to_default(void)
{
    static const char *mode_strings[] = {
        [NORMAL_MODE] = "",

        [INSERT_MODE] = "-- INSERT --",

        [VISUAL_MODE] = "-- VISUAL --",
        [VISUAL_LINE_MODE] = "-- VISUAL LINE --",
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

int load_last_session(void)
{
    DIR *dir;
    struct dirent *ent;
    struct stat st;
    char *name;
    char *latest_name;
    time_t latest_time;
    FILE *fp;

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

void save_current_session(void)
{
    FILE *fp;
    time_t cur_time;
    char *name;
    struct tm *tm;

    cur_time = time(NULL);
    tm = localtime(&cur_time);

    name = xasprintf("%s/session_%04d-%02d-%02d_%02d-%02d-%02d",
            Core.session_dir,
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec);

    fp = fopen(name, "wb");
    if (fp != NULL) {
        save_session(fp);
        fclose(fp);
    }

    free(name);
}

int main(int argc, char **argv)
{
    static const char cursors[] = {
        [NORMAL_MODE] = '\x30',

        [INSERT_MODE] = '\x35',

        [VISUAL_MODE] = '\x30',
        [VISUAL_LINE_MODE] = '\x30',
        [VISUAL_BLOCK_MODE] = '\x30',
    };

   static int (*const input_handlers[])(int c) = {
        [NORMAL_MODE] = normal_handle_input,
        [INSERT_MODE] = insert_handle_input,
        [VISUAL_MODE] = visual_handle_input,
        [VISUAL_BLOCK_MODE] = visual_handle_input,
        [VISUAL_LINE_MODE] = visual_handle_input,
    };

    const char *home;
    int cur_x, cur_y;
    size_t first_event;
    int c;
    int r;
    size_t next_dot_i;
    int old_mode;
    struct frame *old_frame;
    struct mark *mark;

    setlocale(LC_ALL, "");

    if (!parse_args(argc, argv) ||  Args.needs_help) {
        usage(stderr, argv[0]);
        return 0;
    }

    home = getenv("HOME");
    if (home == NULL) {
        fprintf(stderr, "could not get environment variable 'HOME': %s\n",
                strerror(errno));
        return 1;
    }

    Core.cache_dir = xasprintf("%s/.cache/purec", home);
    if (mkdir(Core.cache_dir, 0755) == -1 && errno != EEXIST) {
        fprintf(stderr, "could not create, '%s/.cache/purec': %s\n",
                home, strerror(errno));
        return 1;
    }

    Core.session_dir = xasprintf("%s/sessions", Core.cache_dir);
    if (mkdir(Core.session_dir, 0755) == -1 && errno != EEXIST) {
        Core.session_dir = xstrdup("/tmp");
        fprintf(stderr, "could not create session directory,"
                "using /tmp instead\n");
    }

    initscr();
    raw();
    keypad(stdscr, true);
    noecho();

    set_tabsize(4);

    set_escdelay(0);

    init_colors();
    init_clipboard();

    Core.msg_win = newpad(1, 128);
    OffScreen = newpad(1, 512);

    wbkgdset(stdscr, ' ' | COLOR_PAIR(HI_NORMAL));
    wbkgdset(Core.msg_win, ' ' | COLOR_PAIR(HI_NORMAL));
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

    while (1) {
        curs_set(0);

        erase();

        for (struct frame *f = FirstFrame; f != NULL; f = f->next) {
            render_frame(f);
        }

        if (Core.msg_state == MSG_TO_DEFAULT) {
            set_message_to_default();
            Core.msg_state = MSG_DEFAULT;
        }
        copywin(Core.msg_win, stdscr, 0, 0, LINES - 1, 0, LINES - 1,
                MIN(COLS - 1, getmaxx(Core.msg_win)), 0);

        if (get_visual_cursor(SelFrame, &cur_x, &cur_y)) {
            move(cur_y, cur_x);
            printf("\x1b[%c q", cursors[Core.mode]);
            fflush(stdout);
            curs_set(1);
        }

        old_frame = SelFrame;
        first_event = SelFrame->buf->event_i;

        do {
            next_dot_i = Core.rec_len;
            if (Core.mode == INSERT_MODE) {
                c = get_ch();
            } else {
                Core.move_down_count = 0;
                do {
                    c = get_first_char();
                } while (c == -1);
            }

            old_mode = Core.mode;
            r = input_handlers[Core.mode](c);
            if (!is_playback() && Core.play_i == SIZE_MAX) {
                if (old_mode == NORMAL_MODE) {
                    if ((r & DO_RECORD)) {
                        Core.dot_i = next_dot_i;
                        Core.dot_e = Core.rec_len;
                    }
                } else {
                    Core.dot_e = Core.rec_len;
                }
            }
        /* does not render the UI if nothing changed or when a recording is
         * playing which means: no active user input
         */
        } while ((r & UPDATE_UI) == 0 || is_playback());

        if (Core.is_stopped) {
            break;
        }

        if (old_frame == SelFrame) {
            if (old_frame->buf->event_i > 0) {
                mark = &Core.marks['.' - MARK_MIN];
                mark->buf = old_frame->buf;
                mark->pos = old_frame->buf->events[old_frame->buf->event_i - 1].cur;
            }
            /* this combines events originating from a single keybind or an
             * entire recording playback
             */
            for (size_t i = first_event + 1; i < SelFrame->buf->event_i; i++) {
                SelFrame->buf->events[i - 1].flags |= IS_TRANSIENT;
            }
        }
    }

    /* restore terminal state */
    endwin();

    save_current_session();

    /* free resources */
    free_session();
    free(Input.buf);
    free(Input.remember);
    return Core.exit_code;
}
