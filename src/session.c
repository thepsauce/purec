#include "frame.h"
#include "purec.h"
#include "xalloc.h"

#include <ctype.h>
#include <string.h>

#define SESSION_HEADER "\x12PC"

void free_session(void)
{
    for (struct frame *frame = FirstFrame, *next; frame != NULL; frame = next) {
        next = frame->next;
        free(frame);
    }

    while (FirstBuffer != NULL) {
        destroy_buffer(FirstBuffer);
    }

    /* clear marks */
    memset(Core.marks, 0, sizeof(Core.marks));

    FirstFrame = NULL;
    FirstBuffer = NULL;
}

void save_session(FILE *fp)
{
    struct buf *buf;
    struct frame *frame;
    size_t num_frames, sel_i;

    num_frames = 0;
    for (frame = FirstFrame; frame != NULL; frame = frame->next) {
        if (frame == SelFrame) {
            sel_i = num_frames;
        }
        num_frames++;
    }

    fprintf(fp, "%s%ld %zu selected\n", SESSION_HEADER, time(NULL), sel_i);

    /* TODO: also store some core data */

    for (buf = FirstBuffer; buf != NULL; buf = buf->next) {
        fprintf(fp, "B%zu %c%s%c %zu,%zu %zu,%zu\n",
                buf->id, '\0', buf->path == NULL ? "" : buf->path, '\0',
                buf->save_cur.col, buf->save_cur.line,
                buf->save_scroll.col, buf->save_scroll.line);
    }

    fputc('\n', fp);
    for (frame = FirstFrame; frame != NULL; frame = frame->next) {
        fprintf(fp, "F%zu %d:%d;%dx%d %zu,%zu %zu,%zu\n",
                frame->buf->id, frame->x, frame->y, frame->w, frame->h,
                frame->cur.col, frame->cur.line,
                frame->scroll.col, frame->scroll.line);
    }
}

static int check_header(FILE *fp)
{
    char header[sizeof(SESSION_HEADER) - 1];

    if (fread(header, 1, sizeof(header), fp) != sizeof(header) ||
            memcmp(SESSION_HEADER, header, sizeof(header)) != 0) {
        return -1;
    }
    return 0;
}

static int skip_to_line_end(FILE *fp)
{
    int c;

    while (c = fgetc(fp), c != '\n' && c != EOF) {
        (void) 0;
    }

    do {
        c = fgetc(fp);
    } while (isspace(c));

    ungetc(c, fp);

    return c;
}

static int load_number_zu(FILE *fp, size_t *p_num)
{
    size_t num;
    int c;

    do {
        c = fgetc(fp);
    } while (isblank(c) || c == ':' || c == ';' || c == ',' || c == 'x');

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
    int sign;
    int num;
    int c;

    do {
        c = fgetc(fp);
    } while (isblank(c) || c == ':' || c == ';' || c == ',' || c == 'x');

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
    int c;
    char *s;
    size_t a, n;
    char *r;

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
        (void) (load_number_zu(fp, &buf->save_cur.col) != 0 ||
                load_number_zu(fp, &buf->save_cur.line) != 0 ||
                load_number_zu(fp, &buf->save_scroll.col) != 0 ||
                load_number_zu(fp, &buf->save_scroll.line) != 0);
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
            load_number_zu(fp, &frame->cur.col) != 0 ||
            load_number_zu(fp, &frame->cur.line) != 0 ||
            load_number_zu(fp, &frame->scroll.col) != 0 ||
            load_number_zu(fp, &frame->scroll.line) != 0);

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
    size_t last_time, sel_i, cur_i;
    int c;
    struct buf *buf, *next_buf;
    struct frame *frame, *next_frame;

    if (fp == NULL || check_header(fp) != 0) {
        FirstBuffer = create_buffer(NULL);
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

    return 0;
}
