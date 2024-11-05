#include "frame.h"
#include "fuzzy.h"
#include "input.h"
#include "purec.h"
#include "xalloc.h"

#include <ctype.h>
#include <string.h>
#include <dirent.h>

#define SESSION_HEADER "\x12PC"

void free_session(void)
{
    struct frame    *frame, *next;

    for (frame = FirstFrame; frame != NULL; frame = next) {
        next = frame->next;
        free(frame);
    }
    FirstFrame = NULL;
    SelFrame = NULL;

    while (FirstBuffer != NULL) {
        destroy_buffer(FirstBuffer);
    }

    /* clear marks */
    memset(Core.marks, 0, sizeof(Core.marks));
}

void save_session(FILE *fp)
{
    struct buf      *buf;
    struct frame    *frame;
    size_t          num_frames, sel_i = 0;
    int             i;

    num_frames = 0;
    for (frame = FirstFrame; frame != NULL; frame = frame->next) {
        if (frame == SelFrame) {
            sel_i = num_frames;
        }
        num_frames++;
    }

    fprintf(fp, "%s%ld %zu selected\n", SESSION_HEADER, time(NULL), sel_i);

    for (buf = FirstBuffer; buf != NULL; buf = buf->next) {
        fprintf(fp, "B%zu %c%s%c "PRCOL","PRLINE" "PRCOL","PRLINE" %zu\n",
                buf->id, '\0', buf->path == NULL ? "" : (char*) buf->path, '\0',
                buf->save_cur.col, buf->save_cur.line,
                buf->save_scroll.col, buf->save_scroll.line,
                buf->lang);
    }

    fputc('\n', fp);
    for (frame = FirstFrame; frame != NULL; frame = frame->next) {
        fprintf(fp, "F%zu %d:%d;%dx%d "PRCOL","PRLINE" "PRCOL","PRLINE"\n",
                frame->buf->id, frame->x, frame->y, frame->w, frame->h,
                frame->cur.col, frame->cur.line,
                frame->scroll.col, frame->scroll.line);
    }

    fprintf(fp, "%d %d\n", Core.rule.tab_size, Core.rule.use_spaces);
    for (i = 0; i <= MARK_MAX - MARK_MIN; i++) {
        fprintf(fp, "%zu "PRLINE","PRCOL"\n",
                Core.marks[i].buf == NULL ? 0 : Core.marks[i].buf->id,
                Core.marks[i].pos.line,
                Core.marks[i].pos.col);
    }
    fprintf(fp, "%zu:\n", Core.rec_len);
    fwrite(Core.rec, Core.rec_len, 1, fp);
    fputc('\n', fp);
    for (i = 0; i <= USER_REC_MAX - USER_REC_MIN; i++) {
        fprintf(fp, "%zu -> %zu\n", Core.user_recs[i].from, Core.user_recs[i].to);
    }
    fprintf(fp, "%zu -> %zu\n", Core.dot.from, Core.dot.to);
}

static int check_header(FILE *fp)
{
    char            header[sizeof(SESSION_HEADER) - 1];

    if (fread(header, 1, sizeof(header), fp) != sizeof(header) ||
            memcmp(SESSION_HEADER, header, sizeof(header)) != 0) {
        return -1;
    }
    return 0;
}

static int skip_to_line_end(FILE *fp)
{
    int             c;

    while (c = fgetc(fp), c != '\n' && c != EOF) {
        (void) 0;
    }

    do {
        c = fgetc(fp);
    } while (isspace(c));

    ungetc(c, fp);

    return c;
}

static bool is_blank_ext(int c)
{
    return isblank(c) || c == ':' || c == ';' || c == ',' || c == 'x' ||
        c == '>';
}

static int load_number_zu(FILE *fp, size_t *p_num)
{
    size_t          num;
    int             c;

    do {
        c = fgetc(fp);
    } while (is_blank_ext(c));

    if (!isdigit(c)) {
        return -1;
    }

    num = 0;
    do {
        num *= 10;
        num += c - '0';
    } while (c = fgetc(fp), isdigit(c));

    ungetc(c, fp);

    *p_num = num;
    return 0;
}

static int load_number_d(FILE *fp, int *p_num)
{
    int             sign;
    int             num;
    int             c;

    do {
        c = fgetc(fp);
    } while (is_blank_ext(c));

    if (c == '+' || c == '-') {
        sign = c == '+' ? 1 : -1;
        c = fgetc(fp);
    } else {
        sign = 1;
    }

    if (!isdigit(c)) {
        if (c == '\n') {
            ungetc(c, fp);
        }
        return -1;
    }

    num = 0;
    do {
        num *= 10;
        num += c - '0';
    } while (c = fgetc(fp), isdigit(c));

    ungetc(c, fp);

    *p_num = sign * num;
    return 0;
}

static int load_number_ld(FILE *fp, long *p_num)
{
    int             sign;
    long            num;
    int             c;

    do {
        c = fgetc(fp);
    } while (is_blank_ext(c));

    if (c == '+' || c == '-') {
        sign = c == '+' ? 1 : -1;
        c = fgetc(fp);
    } else {
        sign = 1;
    }

    if (!isdigit(c)) {
        if (c == '\n') {
            ungetc(c, fp);
        }
        return -1;
    }

    num = 0;
    do {
        num *= 10;
        num += c - '0';
    } while (c = fgetc(fp), isdigit(c));

    ungetc(c, fp);

    *p_num = sign * num;
    return 0;
}

static char *load_string(FILE *fp)
{
    int             c;
    char            *s;
    size_t          a, n;
    char            *r;

    do {
        c = fgetc(fp);
    } while (isblank(c));

    if (c != '\0') {
        if (c == '\n') {
            ungetc(c, fp);
        }
        return NULL;
    }

    a = 8;
    s = xmalloc(a);
    n = 0;
    while (1) {
        c = fgetc(fp);
        if (c == EOF) {
            free(s);
            return NULL;
        }
        if (c == '\0') {
            break;
        }
        if (n + 1 == a) {
            a *= 2;
            s = xrealloc(s, a);
        }
        s[n++] = c;
    }
    r = xmalloc(n + 1);
    memcpy(r, s, n);
    r[n] = '\0';
    free(s);
    return r;
}

static struct buf *load_buffer(FILE *fp)
{
    struct buf *buf;

    buf = xcalloc(1, sizeof(*buf));

    load_number_zu(fp, &buf->id);
    buf->path = load_string(fp);
    if (buf->path != NULL) {
        if (buf->path[0] == '\0') {
            free(buf->path);
            buf->path = NULL;
        }
        /* this just makes sure we stop after a failed call */
        (void) (load_number_d(fp, &buf->save_cur.col) != 0 ||
                load_number_ld(fp, &buf->save_cur.line) != 0 ||
                load_number_d(fp, &buf->save_scroll.col) != 0 ||
                load_number_ld(fp, &buf->save_scroll.line) != 0 ||
                load_number_zu(fp, &buf->lang) != 0);
    }

    init_load_buffer(buf);

    return buf;
}

static struct frame *load_frame(FILE *fp)
{
    struct frame *frame;
    size_t buf_id;

    frame = xcalloc(1, sizeof(*frame));

    (void) (load_number_zu(fp, &buf_id) != 0 ||
            load_number_d(fp, &frame->x) != 0 ||
            load_number_d(fp, &frame->y) != 0 ||
            load_number_d(fp, &frame->w) != 0 ||
            load_number_d(fp, &frame->h) != 0 ||
            load_number_d(fp, &frame->cur.col) != 0 ||
            load_number_ld(fp, &frame->cur.line) != 0 ||
            load_number_d(fp, &frame->scroll.col) != 0 ||
            load_number_ld(fp, &frame->scroll.line) != 0);

    if (frame->w <= 0) {
        frame->w = 1;
    }

    if (frame->h <= 0) {
        frame->h = 1;
    }

    frame->buf = get_buffer(buf_id);
    if (frame->buf == NULL) {
        frame->buf = FirstBuffer;
    }

    /* clip the cursor and adjust scrolling */
    set_cursor(frame, &frame->cur);

    return frame;
}

int load_session(FILE *fp)
{
    size_t          last_time, sel_i, cur_i;
    int             c;
    struct buf      *buf, *next_buf;
    struct frame    *frame, *next_frame;
    int             i;
    size_t          buf_id;
    int             use_spaces;

    if (fp == NULL || check_header(fp) != 0) {
        FirstBuffer = create_buffer(NULL);
        FirstFrame = create_frame(NULL, 0, FirstBuffer);
        SelFrame = FirstFrame;
        return -1;
    }

    (void) (load_number_zu(fp, &last_time) != 0 ||
            load_number_zu(fp, &sel_i) != 0);

    buf = NULL;
    frame = NULL;
    cur_i = 0;
    while (skip_to_line_end(fp) != EOF) {
        c = fgetc(fp);
        if (c == 'B') {
            next_buf = load_buffer(fp);

            if (buf == NULL) {
                FirstBuffer = next_buf;
            } else {
                buf->next = next_buf;
            }
            buf = next_buf;
        } else if (c == 'F') {
            next_frame = load_frame(fp);

            cur_i++;
            if (frame == NULL) {
                FirstFrame = next_frame;
            } else {
                frame->next = next_frame;
            }
            frame = next_frame;
            if (cur_i == sel_i) {
                SelFrame = frame;
            }
        }
    }

    if (FirstBuffer == NULL) {
        FirstBuffer = create_buffer(NULL);
    }

    /* make sure all frames have a buffer */
    for (frame = FirstFrame; frame != NULL; frame = frame->next) {
        if (frame->buf == NULL) {
            frame->buf = FirstBuffer;
            break;
        }
    }

    if (FirstFrame == NULL) {
        frame = create_frame(NULL, 0, FirstBuffer);
        FirstFrame = frame;
    }

    if (SelFrame == NULL) {
        SelFrame = FirstFrame;
    }

    /* the screen size back then might be different than the current one */
    update_screen_size();

    load_number_d(fp, &Core.rule.tab_size);
    load_number_d(fp, &use_spaces);
    Core.rule.use_spaces = use_spaces;
    for (i = 0; i <= MARK_MAX - MARK_MIN; i++) {
        if (load_number_zu(fp, &buf_id) == -1) {
            break;
        }
        Core.marks[i].buf = get_buffer(buf_id);
        load_number_d(fp, &Core.marks[i].pos.col);
        load_number_ld(fp, &Core.marks[i].pos.line);
    }
    return 0;
}

static void update_files(struct fuzzy *fuzzy)
{
    DIR             *dir;
    size_t          i;
    struct dirent   *ent;
    size_t          a;

    dir = opendir(Core.session_dir);
    if (dir == NULL) {
        return;
    }

    for (i = 0; i < fuzzy->num_entries; i++) {
        free(fuzzy->entries[i].name);
    }
    a = fuzzy->num_entries;
    fuzzy->num_entries = 0;
    while (ent = readdir(dir), ent != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        if (fuzzy->num_entries == a) {
            a *= 2;
            a++;
            fuzzy->entries = xreallocarray(fuzzy->entries, a,
                                        sizeof(*fuzzy->entries));
        }
        fuzzy->entries[fuzzy->num_entries].type = ent->d_type;
        fuzzy->entries[fuzzy->num_entries].name = xstrdup(ent->d_name);
        fuzzy->num_entries++;
    }
    closedir(dir);
    sort_entries(fuzzy);
}

char *save_current_session(void)
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
    return name;
}

void choose_session(void)
{
    struct fuzzy    fuzzy;
    int             c;
    FILE            *fp;
    char            *last_session;
    char            *path;
    char            *prev_name;

    memset(&fuzzy, 0, sizeof(fuzzy));
    last_session = save_current_session();
    update_files(&fuzzy);
    while (1) {
        render_fuzzy(&fuzzy);
        c = get_ch();
        prev_name = fuzzy.entries[fuzzy.selected].name;
        switch (send_to_fuzzy(&fuzzy, c)) {
        case INP_CANCELLED:
            /* revert to the last loaded session */
            path = xasprintf("%s/%s", Core.session_dir, last_session);
            free(last_session);
            fp = fopen(path, "rb");
            free(path);
            if (fp != NULL) {
                free_session();
                load_session(fp);
                fclose(fp);
            }
            clear_fuzzy(&fuzzy);
            return;

        case INP_FINISHED:
            /* keep the currently loaded session */
            free(last_session);
            clear_fuzzy(&fuzzy);
            return;
        }
        if (prev_name != fuzzy.entries[fuzzy.selected].name) {
            path = xasprintf("%s/%s", Core.session_dir,
                             fuzzy.entries[fuzzy.selected].name);
            fp = fopen(path, "rb");
            free(path);
            if (fp != NULL) {
                free_session();
                load_session(fp);
                fclose(fp);
                render_all();
            }
        }
    }
}
