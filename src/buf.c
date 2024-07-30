#include "buf.h"
#include "util.h"
#include "xalloc.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <ncurses.h>

/* TODO: buffer list */

static int reload_file(struct buf *buf)
{
    FILE *fp;
    char *s_line = NULL;
    size_t a_line = 0;
    ssize_t n_line;
    struct line *line;
    size_t num_old;

    fp = fopen(buf->path, "r");
    if (fp == NULL) {
        printf("failed opening: %s\n", buf->path);
        return -1;
    }

    num_old = buf->num_lines;

    buf->num_lines = 0;
    while (n_line = getline(&s_line, &a_line, fp), n_line > 0) {
        if (s_line[n_line - 1] == '\n') {
            n_line--;
        }
        line = _insert_lines(buf, buf->num_lines, 1);
        line->s = xmalloc(n_line);
        memcpy(line->s, s_line, n_line);
        line->n = n_line;
    }

    for (size_t i = buf->num_lines; i < num_old; i++) {
        free(buf->lines[i].s);
    }
    buf->lines = xreallocarray(buf->lines, buf->num_lines, sizeof(*buf->lines));
    buf->a_lines = buf->num_lines;

    free(s_line);
    return 0;
}

struct buf *create_buffer(const char *path)
{
    struct buf *buf;

    buf = xcalloc(1, sizeof(*buf));
    if (path != NULL) {
        buf->path = xstrdup(path);
        if (stat(path, &buf->st) == 0) {
            (void) reload_file(buf);
            if (buf->num_lines > 0) {
                return buf;
            }
        }
    }
    buf->lines = xcalloc(1, sizeof(*buf->lines));
    buf->num_lines = 1;
    buf->a_lines = 1;
    return buf;
}

void delete_buffer(struct buf *buf)
{
    free(buf->path);
    for (size_t i = 0; i < buf->num_lines; i++) {
        free(buf->lines[i].s);
    }
    free(buf->lines);
    for (size_t i = 0; i < buf->num_events; i++) {
        free(buf->events[i].text);
    }
    free(buf->events);
    free(buf);
}

struct undo_event *add_event(struct buf *buf, const struct undo_event *copy_ev)
{
    struct undo_event *ev;

    buf->events = xreallocarray(buf->events, buf->event_i + 1,
            sizeof(*buf->events));
    ev = &buf->events[buf->event_i++];
    buf->num_events = buf->event_i;
    *ev = *copy_ev;
    ev->is_transient = false;
    ev->time = time(NULL);
    return ev;
}

struct undo_event *undo_event(struct buf *buf)
{
    struct undo_event *ev, *first = NULL;
    size_t line, col;
    size_t m;
    size_t l;
    struct pos from, to;

    if (buf->event_i == 0) {
        return NULL;
    }
next_event:
    ev = &buf->events[--buf->event_i];
    if (first == NULL) {
        first = ev;
    }
    line = ev->pos.line;
    col = ev->pos.col;

    m = MIN(ev->ins_len, ev->del_len);
    for (size_t i = 0; i < m; i++) {
        if (col == buf->lines[line].n) {
            line++;
            col = 0;
        }
        buf->lines[line].s[col] ^= ev->text[i];
        col++;
    }

    from.col = col;
    from.line = line;
    if (ev->del_len > ev->ins_len) {
        _insert_text(buf, &from, &ev->text[ev->ins_len],
                ev->del_len - ev->ins_len, 1);
    } else if (ev->del_len < ev->ins_len) {
        /* find the end of deletion */
        l = ev->ins_len - ev->del_len;
        while (l > buf->lines[line].n - col) {
            l -= buf->lines[line].n - col + 1;
            line++;
            col = 0;
        }

        to.line = line;
        to.col = col + l;
        _delete_range(buf, &from, &to);
    }
    if (ev->is_transient && buf->event_i > 0) {
        goto next_event;
    }
    return first;
}

struct undo_event *redo_event(struct buf *buf)
{
    struct undo_event *ev, *first = NULL;
    size_t line;
    size_t col;
    size_t m;
    size_t l;
    struct pos from, to;

    if (buf->event_i == buf->num_events) {
        return NULL;
    }
next_event:
    ev = &buf->events[buf->event_i++];
    if (first == NULL) {
        first = ev;
    }
    line = ev->pos.line;
    col = ev->pos.col;

    /* XOR the first part */
    m = MIN(ev->ins_len, ev->del_len);
    for (size_t i = 0; i < m; i++) {
        if (col == buf->lines[line].n) {
            line++;
            col = 0;
        }
        buf->lines[line].s[col] ^= ev->text[i];
        col++;
    }

    from.line = line;
    from.col = col;
    if (ev->del_len > ev->ins_len) {
        /* find the end of deletion */
        l = ev->del_len - ev->ins_len;
        while (l > buf->lines[line].n - col) {
            l -= buf->lines[line].n - col + 1;
            line++;
            col = 0;
        }

        to.line = line;
        to.col = col + l;
        _delete_range(buf, &from, &to);
    } else if (ev->del_len < ev->ins_len) {
        _insert_text(buf, &from, &ev->text[ev->del_len],
                ev->ins_len - ev->del_len, 1);
    }
    if (ev->is_transient && buf->event_i != buf->num_events) {
        goto next_event;
    }
    return first;
}

size_t get_line_indent(struct buf *buf, size_t line_i)
{
    size_t col = 0;
    struct line *line;

    line = &buf->lines[line_i];
    while (col < line->n && isblank(line->s[col])) {
        col++;
    }
    return col;
}

struct line *insert_lines(struct buf *buf, size_t line_i, size_t num_lines)
{
    struct line *line;

    if (line_i > buf->num_lines) {
        line_i = buf->num_lines;
    }
    line = _insert_lines(buf, line_i, num_lines);
    memset(&line[0], 0, sizeof(*line) * num_lines);
    return line;
}

struct line *_insert_lines(struct buf *buf, size_t line_i, size_t num_lines)
{
    if (buf->num_lines + num_lines > buf->a_lines) {
        buf->a_lines *= 2;
        buf->a_lines += num_lines;
        buf->lines = xreallocarray(buf->lines, buf->a_lines, sizeof(*buf->lines));
    }
    /* move tail of the line list to form a gap */
    memmove(&buf->lines[line_i + num_lines], &buf->lines[line_i],
            sizeof(*buf->lines) * (buf->num_lines - line_i));
    buf->num_lines += num_lines;
    return &buf->lines[line_i];
}

struct undo_event *indent_line(struct buf *buf, size_t line_i)
{
    /* first naive approach: match indentation of previous line */

    size_t cur_indent, new_indent = 0;
    struct pos pos, to;
    char sp[1] = { ' ' };

    if (line_i == 0) {
        new_indent = 0;
    } else {
        new_indent = get_line_indent(buf, line_i - 1);
    }
    cur_indent = get_line_indent(buf, line_i);
    pos.line = line_i;
    pos.col = 0;
    if (new_indent > cur_indent) {
        return insert_text(buf, &pos, sp, 1, new_indent - cur_indent);
    }
    to.line = line_i;
    to.col = cur_indent - new_indent;
    return delete_range(buf, &pos, &to);
}

struct undo_event *insert_text(struct buf *buf, struct pos *pos,
        const char *text, size_t len_text, size_t repeat)
{
    size_t prod;
    struct undo_event ev, *pev;

    if (len_text == 0 || repeat == 0) {
        return 0;
    }

    /* reduce repeat so that `len_text * repeat` fits into a `SIZE_MAX` */
    if (__builtin_mul_overflow(len_text, repeat, &prod)) {
        repeat = SIZE_MAX / len_text;
    }

    ev.pos = *pos;
    ev.ins_len = prod;
    ev.del_len = 0;
    ev.text = xmalloc(ev.ins_len);
    for (size_t i = 0; i < repeat; i++) {
        memcpy(&ev.text[i * len_text], text, len_text);
    }
    pev = add_event(buf, &ev);

    _insert_text(buf, pos, text, len_text, repeat);
    return pev;
}

void _insert_text(struct buf *buf, struct pos *pos,
        const char *text, size_t len_text, size_t repeat)
{
    size_t i, j;
    size_t num_lines = 0;
    struct line *line;
    size_t n;
    struct line *at_line;
    size_t first_end;

    for (i = 0; i < len_text; i++) {
        if (text[i] == '\n') {
            if (num_lines == 0) {
                j = i;
            }
            num_lines++;
        }
    }

    if (num_lines == 0) {
        line = &buf->lines[pos->line];
        n = len_text * repeat;
        line->s = xrealloc(line->s, line->n + n);
        memmove(&line->s[pos->col + n], &line->s[pos->col],
                line->n - pos->col);
        for (size_t i = pos->col; repeat > 0; repeat--) {
            memcpy(&line->s[i], text, len_text);
            i += len_text;
        }
        line->n += n;
        return;
    }

    first_end = j;
    line = _insert_lines(buf, pos->line + 1, num_lines * repeat);

    for (; repeat > 0; repeat--) {
        do {
            i = j + 1;
            /* find the next new line */
            for (j = i; j < len_text && text[j] != '\n'; j++) {
                (void) 0;
            }
            line->n = j - i;
            line->s = xmalloc(line->n);
            memcpy(&line->s[0], &text[i], line->n);
            line++;
        } while (j != len_text);
        j = SIZE_MAX;
    }

    at_line = &buf->lines[pos->line];

    /* add the end of the first line to the front of the last line */
    /* `line` overshoots the last line, so decrement it */
    line--;
    line->n += at_line->n - pos->col;
    line->s = xrealloc(line->s, line->n);
    memmove(&line->s[at_line->n - pos->col], &line->s[0],
            line->n - (at_line->n - pos->col));
    memcpy(&line->s[0], &at_line->s[pos->col], at_line->n - pos->col);

    /* trim first line and insert first text segment */
    at_line->n = pos->col + first_end;
    at_line->s = xrealloc(at_line->s, at_line->n);
    memcpy(&at_line->s[pos->col], &text[0], first_end);
}

struct undo_event *delete_range(struct buf *buf, const struct pos *pfrom,
        const struct pos *pto)
{
    struct pos from, to;
    struct undo_event ev, *pev;
    size_t del_i;

    from = *pfrom;
    to = *pto;

    /* clip lines */
    if (from.line >= buf->num_lines) {
        from.line = buf->num_lines - 1;
    }
    /* to is allowed to go out of bounds */
    if (to.line > buf->num_lines) {
        to.line = buf->num_lines;
    }

    /* swap if needed */
    sort_positions(&from, &to);

    /* clip columns */
    from.col = MIN(from.col, buf->lines[from.line].n);
    if (to.line == buf->num_lines) {
        to.line--;
        to.col = buf->lines[to.line].n;
    } else {
        to.col = MIN(to.col, buf->lines[to.line].n);
    }

    /* make sure there is text to delete */
    if (from.line == to.line && from.col == to.col) {
        return 0;
    }

    /* get the deleted text for undo */
    ev.pos = from;
    ev.ins_len = 0;
    if (from.line != to.line) {
        ev.del_len = buf->lines[from.line].n - from.col + 1;
        for (size_t i = from.line + 1; i < to.line; i++) {
            ev.del_len += buf->lines[i].n + 1;
        }
        ev.del_len += to.col;
        ev.text = xmalloc(ev.del_len);

        del_i = 0;
        memcpy(&ev.text[del_i], &buf->lines[from.line].s[from.col],
                buf->lines[from.line].n - from.col);
        del_i += buf->lines[from.line].n - from.col;
        ev.text[del_i++] = '\n';

        for (size_t i = from.line + 1; i < to.line; i++) {
            memcpy(&ev.text[del_i], buf->lines[i].s, buf->lines[i].n);
            del_i += buf->lines[i].n;
            ev.text[del_i++] = '\n';
        }
        if (to.line != buf->num_lines) {
            memcpy(&ev.text[del_i], &buf->lines[to.line].s[0], to.col);
        }
    } else {
        ev.del_len = to.col - from.col;
        ev.text = xmalloc(ev.del_len);
        memcpy(&ev.text[0], &buf->lines[from.line].s[from.col], ev.del_len);
    }
    pev = add_event(buf, &ev);

    _delete_range(buf, &from, &to);
    return pev;
}

void _delete_range(struct buf *buf, const struct pos *pfrom, const struct pos *pto)
{
    struct pos from, to;
    struct line *fl, *tl;

    from = *pfrom;
    to = *pto;

    fl = &buf->lines[from.line];
    if (from.line == to.line) {
        memmove(&fl->s[from.col], &fl->s[to.col], fl->n - to.col);
        fl->n -= to.col - from.col;
    } else if (to.line >= buf->num_lines) {
        /* trim the line */
        fl->n = from.col;
        if (from.line > 0) {
            from.line--;
        }
        /* delete the remaining lines */
        for (size_t i = from.line + 1; i < buf->num_lines; i++) {
            free(buf->lines[i].s);
        }
        buf->num_lines = from.line + 1;
    } else {
        tl = &buf->lines[to.line];
        /* join the current line with the last line */
        fl->n = from.col + tl->n - to.col;
        fl->s = xrealloc(fl->s, fl->n);
        memcpy(&fl->s[from.col], &tl->s[to.col], tl->n - to.col);
        /* delete the remaining lines */
        for (size_t i = from.line + 1; i <= to.line; i++) {
            free(buf->lines[i].s);
        }
        memmove(&fl[1], &tl[1], sizeof(*buf->lines) *
                (buf->num_lines - to.line - 1));
        buf->num_lines -= to.line - from.line;
    }
}
