#include "buf.h"
#include "util.h"
#include "xalloc.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int reload_file(struct buf *buf)
{
    FILE *fp;
    char *s_line = NULL;
    size_t a_line = 0;
    ssize_t n_line;
    struct line *line;

    fp = fopen(buf->path, "r");
    if (fp == NULL) {
        printf("failed opening: %s\n", buf->path);
        return -1;
    }
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
            return buf;
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

void add_event(struct buf *buf, struct undo_event *ev)
{
    buf->events = xreallocarray(buf->events, buf->event_i + 1,
            sizeof(*buf->events));
    buf->events[buf->event_i++] = *ev;
    buf->num_events = buf->event_i;
}

struct undo_event *undo_event(struct buf *buf)
{
    struct undo_event *ev;
    size_t line, col;
    size_t m;
    size_t l;
    struct pos from, to;

    if (buf->event_i == 0) {
        return NULL;
    }
    ev = &buf->events[--buf->event_i];
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
                ev->del_len - ev->ins_len);
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
    return ev;
}

struct undo_event *redo_event(struct buf *buf)
{
    struct undo_event *ev;
    size_t line;
    size_t col;
    size_t m;
    size_t l;
    struct pos from, to;

    if (buf->event_i == buf->num_events) {
        return NULL;
    }
    ev = &buf->events[buf->event_i++];
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
                ev->ins_len - ev->del_len);
    }
    return ev;
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

int insert_text(struct buf *buf, struct pos *pos,
        const char *text, size_t len_text)
{
    struct undo_event ev;

    if (len_text == 0) {
        return 0;
    }
    ev.pos = *pos;
    ev.cur = *pos;
    ev.ins_len = len_text;
    ev.del_len = 0;
    ev.text = xmalloc(len_text);
    memcpy(ev.text, text, len_text);
    add_event(buf, &ev);

    _insert_text(buf, pos, text, len_text);
    return 1;
}

void _insert_text(struct buf *buf, struct pos *pos,
        const char *text, size_t len_text)
{
    struct line *at_line;
    struct line *line;
    size_t num_lines = 0;
    size_t i, j;
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
        line->s = xrealloc(line->s, line->n + len_text);
        memmove(&line->s[pos->col + len_text], &line->s[pos->col],
                line->n - pos->col);
        memcpy(&line->s[pos->col], text, len_text);
        line->n += len_text;
        return;
    }

    line = _insert_lines(buf, pos->line + 1, num_lines);
    at_line = &buf->lines[pos->line];
    first_end = j;
    while (i = j + 1, 1) {
        /* find the next new line */
        for (j = i; j < len_text && text[j] != '\n'; j++) {
            (void) 0;
        }
        if (j == len_text) {
            line->n = j - i + at_line->n - pos->col;
            line->s = xmalloc(line->n);
            memcpy(&line->s[0], &text[i], j - i);
            memcpy(&line->s[j - i], &at_line->s[pos->col],
                    at_line->n - pos->col);
            break;
        }
        line->n = j - i;
        line->s = xmalloc(line->n);
        memcpy(&line->s[0], &text[i], line->n);
        line++;
    }

    at_line->n = pos->col + first_end;
    at_line->s = xrealloc(at_line->s, at_line->n);
    memcpy(&at_line->s[pos->col], &text[0], first_end);
}

int delete_lines(struct buf *buf, const struct pos *pos, size_t num_lines)
{
    size_t end;
    struct undo_event ev;
    size_t del_i;

    if (pos->line >= buf->num_lines) {
        return 0;
    }
    if (__builtin_add_overflow(pos->line, num_lines, &end)) {
        end = SIZE_MAX;
    }
    if (end >= buf->num_lines) {
        end = buf->num_lines;
    }
    num_lines = end - pos->line;
    if (num_lines == 0) {
        return 0;
    }

    if (buf->num_lines == 1 && buf->lines[0].n == 0) {
        return 0;
    }

    ev.cur = *pos;
    /* get the deleted text for undo */
    ev.pos.line = pos->line;
    ev.pos.col = 0;
    ev.ins_len = 0;
    ev.del_len = 0;
    for (size_t i = pos->line; i < end; i++) {
        ev.del_len += buf->lines[i].n + 1;
    }
    ev.text = xmalloc(ev.del_len);

    del_i = 0;
    for (size_t i = pos->line; i < end; i++) {
        memcpy(&ev.text[del_i], buf->lines[i].s,
                buf->lines[i].n);
        del_i += buf->lines[i].n;
        ev.text[del_i++] = '\n';
        free(buf->lines[i].s);
    }
    add_event(buf, &ev);

    buf->num_lines -= num_lines;
    if (buf->num_lines == 0) {
        del_i--;
    }

    /* move tail over gap */
    memmove(&buf->lines[pos->line], &buf->lines[end],
            sizeof(*buf->lines) * (buf->num_lines - pos->line));
    if (buf->num_lines == 0) {
        buf->num_lines = 1;
        buf->lines[0].n = 0;
        buf->lines[0].s = NULL;
    }
    return 1;
}

int delete_range(struct buf *buf, const struct pos *pfrom, const struct pos *pto)
{
    struct pos from, to;
    struct pos tmp;
    struct line *fl, *tl;
    struct undo_event ev;
    size_t del_i;

    from = *pfrom;
    to = *pto;

    /* clip lines */
    if (from.line >= buf->num_lines) {
        from.line = buf->num_lines - 1;
    }
    if (to.line > buf->num_lines) {
        to.line = buf->num_lines;
    }

    /* swap if needed */
    if (from.line > to.line) {
        tmp = from;
        from = to;
        to = tmp;
    } else if (from.line == to.line) {
        if (from.col <= to.col) {
            from.col = pfrom->col;
            to.col = pto->col;
        } else {
            from.col = pto->col;
            to.col = pfrom->col;
        }
    }

    /* clip columns */
    fl = &buf->lines[from.line];
    if (from.col > fl->n) {
        from.col = fl->n;
    }
    tl = &buf->lines[to.line];
    if (to.col > tl->n) {
        to.col = tl->n;
    }

    if (from.line == to.line && from.col == to.col) {
        return 0;
    }

    /* get the deleted text for undo */
    ev.cur = *pfrom;
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
    add_event(buf, &ev);

    _delete_range(buf, &from, &to);
    return 1;
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
        memmove(&fl[1], &tl[1],
                sizeof(*buf->lines) * (buf->num_lines - to.line));
        buf->num_lines -= to.line - from.line;
    }
}
