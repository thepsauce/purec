#include "buf.h"
#include "xalloc.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

struct buf *FirstBuffer;

size_t write_file(struct buf *buf, size_t from, size_t to, FILE *fp)
{
    struct line *line;
    size_t num_bytes = 0;

    to = MIN(to, buf->num_lines - 1);

    if (from > to) {
        return 0;
    }

    from = MIN(from, buf->num_lines - 1);

    for (size_t i = from; i <= to; i++) {
        line = &buf->lines[i];
        fwrite(line->s, 1, line->n, fp);
        num_bytes += line->n;
        if (i + 1 != buf->num_lines || line->n != 0) {
            fputc('\n', fp);
            num_bytes++;
        }
    }
    return num_bytes;
}

struct undo_event *read_file(struct buf *buf, const struct pos *pos, FILE *fp)
{
    struct raw_line *lines = NULL;
    size_t a_lines = 0;
    size_t num_lines = 0;
    char *s_line = NULL;
    size_t a_line = 0;
    ssize_t line_len;
    struct undo_event *ev;

    while (line_len = getline(&s_line, &a_line, fp), line_len > 0) {
        for (; line_len > 0; line_len--) {
            if (s_line[line_len - 1] != '\n' &&
                    s_line[line_len - 1] != '\r') {
                break;
            }
        }
        if (num_lines == a_lines) {
            a_lines *= 2;
            a_lines++;
            lines = xreallocarray(lines, a_lines, sizeof(*lines));
        }
        lines[num_lines].s = xmemdup(s_line, line_len);
        lines[num_lines].n = line_len;
    }

    free(s_line);

    ev = insert_lines(buf, pos, lines, num_lines, 1);
    for (size_t i = 0; i < num_lines; i++) {
        free(lines[i].s);
    }
    free(lines);
    return ev;
}

static int reload_file(struct buf *buf)
{
    FILE *fp;
    char *s_line = NULL;
    size_t a_line = 0;
    ssize_t line_len;
    struct line *line;
    size_t num_old;

    fp = fopen(buf->path, "r");
    if (fp == NULL) {
        werase(Message);
        wprintw(Message, "failed opening '%s': %s\n",
                buf->path, strerror(errno));
        return -1;
    }

    num_old = buf->num_lines;

    buf->num_lines = 0;
    while (line_len = getline(&s_line, &a_line, fp), line_len > 0) {
        for (; line_len > 0; line_len--) {
            if (s_line[line_len - 1] != '\n' &&
                    s_line[line_len - 1] != '\r') {
                break;
            }
        }
        line = grow_lines(buf, buf->num_lines, 1);
        line->flags = 0;
        line->state = 0;
        line->attribs = NULL;
        line->s = xmalloc(line_len);
        memcpy(line->s, s_line, line_len);
        line->n = line_len;
    }

    for (size_t i = buf->num_lines; i < num_old; i++) {
        clear_line(&buf->lines[i]);
    }
    buf->lines = xreallocarray(buf->lines, buf->num_lines, sizeof(*buf->lines));
    buf->a_lines = buf->num_lines;

    free(s_line);
    return 0;
}

struct buf *create_buffer(const char *path)
{
    char *rel_path;
    struct buf *buf, *prev;

    /* check if a buffer with that path exists already */
    if (path != NULL) {
        rel_path = get_relative_path(path);
        for (buf = FirstBuffer; buf != NULL; buf = buf->next) {
            if (buf->path == NULL) {
                continue;
            }
            if (strcmp(buf->path, rel_path) == 0) {
                free(rel_path);
                return buf;
            }
        }
    } else {
        rel_path = NULL;
    }

    buf = xcalloc(1, sizeof(*buf));
    if (rel_path != NULL) {
        buf->path = rel_path;
        if (stat(rel_path, &buf->st) == 0) {
            (void) reload_file(buf);
        }
    }

    /* make sure the buffer has at least one line */
    if (buf->num_lines == 0) {
        buf->lines = xcalloc(1, sizeof(*buf->lines));
        buf->num_lines = 1;
        buf->a_lines = 1;
    }

    /* xcalloc does this alread */
    //buf->min_dirty_i = 0;
    buf->max_dirty_i = buf->num_lines - 1;

    /* add buffer to linked list */
    if (FirstBuffer == NULL) {
        FirstBuffer = buf;
    } else {
        /* find next id and insert */
        for (prev = FirstBuffer; prev->next != NULL; ) {
            if (prev->id + 1 != prev->next->id) {
                break;
            }
            prev = prev->next;
        }
        buf->next = prev->next;
        prev->next = buf;
        buf->id = prev->id + 1;
    }

    return buf;
}

void destroy_buffer(struct buf *buf)
{
    struct buf *prev;

    free(buf->path);
    for (size_t i = 0; i < buf->num_lines; i++) {
        clear_line(&buf->lines[i]);
    }
    free(buf->lines);
    for (size_t i = 0; i < buf->num_events; i++) {
        free_event(buf->events[i]);
    }
    free(buf->events);

    /* remove from linked list */
    if (FirstBuffer == buf) {
        FirstBuffer = buf->next;
    } else {
        for (prev = FirstBuffer; prev->next != NULL; ) {
            prev = prev->next;
        }
        prev->next = buf->next;
    }

    free(buf);
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

struct undo_event *insert_lines(struct buf *buf, const struct pos *pos,
        const struct raw_line *c_lines, size_t c_num_lines, size_t repeat)
{
    struct undo_event ev;
    struct raw_line *lines, *line;
    size_t num_lines;

    if (c_num_lines == 0 || repeat == 0) {
        return NULL;
    }

    /* duplicate the lines */
    num_lines = 1 + (c_num_lines - 1) * repeat;
    lines = xreallocarray(NULL, num_lines, sizeof(*lines));
    line = &lines[0];
    if (c_num_lines == 1) {
        line->n = c_lines->n * repeat;
        line->s = xmalloc(c_lines->n * repeat);
        for (size_t r = 0; r < repeat; r++) {
            memcpy(&line->s[r * c_lines->n], &c_lines->s[0], c_lines->n);
        }
    } else {
        init_raw_line(line, &c_lines[0].s[0], c_lines[0].n);
        line++;
        for (size_t r = 0;;) {
            for (size_t i = 1; i < c_num_lines; i++) {
                init_raw_line(line, &c_lines[i].s[0], c_lines[i].n);
                line++;
            }
            r++;
            if (r >= repeat) {
                break;
            }
            if (lines[0].n > 0) {
                line[-1].s = xrealloc(line[-1].s, line[-1].n + lines[0].n);
                memcpy(&line[-1].s[line[-1].n], &lines[0].s[0], lines[0].n);
                line[-1].n += lines[0].n;
            }
        }
    }

    _insert_lines(buf, pos, lines, num_lines);

    ev.flags = IS_INSERTION;
    ev.pos = *pos;
    ev.lines = lines;
    ev.num_lines = num_lines;
    return add_event(buf, &ev);
}

void update_dirty_lines(struct buf *buf, size_t from, size_t to)
{
    buf->min_dirty_i = MIN(buf->min_dirty_i, from);
    buf->max_dirty_i = MAX(buf->max_dirty_i, to);
}

void _insert_lines(struct buf *buf, const struct pos *pos,
        const struct raw_line *lines, size_t num_lines)
{
    const struct raw_line *raw_line;
    struct line *line, *at_line;
    size_t old_n;

    update_dirty_lines(buf, pos->line, pos->line + num_lines - 1);

    if (num_lines == 1) {
        raw_line = &lines[0];
        line = &buf->lines[pos->line];
        line->s = xrealloc(line->s, line->n + raw_line->n);
        memmove(&line->s[pos->col + raw_line->n], &line->s[pos->col],
                line->n - pos->col);
        memcpy(&line->s[pos->col], &raw_line->s[0], raw_line->n);
        line->n += raw_line->n;
        mark_dirty(line);
        return;
    }

    line = grow_lines(buf, pos->line + 1, num_lines - 1);
    for (size_t i = 1; i < num_lines; i++) {
        raw_line = &lines[i];
        line->flags = 0;
        line->state = 0;
        line->attribs = NULL;
        line->n = raw_line->n;
        line->s = xmalloc(line->n);
        memcpy(&line->s[0], &raw_line->s[0], raw_line->n);
        line++;
    }

    at_line = &buf->lines[pos->line];
    raw_line = &lines[num_lines - 1];

    /* add the end of the first line to the end of the last line */
    /* `line` overshoots the last line, so decrement it */
    line--;
    old_n = line->n;
    line->n += at_line->n - pos->col;
    line->s = xrealloc(line->s, line->n);
    memcpy(&line->s[old_n], &at_line->s[pos->col], at_line->n - pos->col);

    /* trim first line and insert first text segment */
    at_line->n = pos->col + lines[0].n;
    at_line->s = xrealloc(at_line->s, at_line->n);
    memcpy(&at_line->s[pos->col], &lines[0].s[0], lines[0].n);

    mark_dirty(at_line);
}

struct line *grow_lines(struct buf *buf, size_t line_i, size_t num_lines)
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
    struct raw_line line;
    struct undo_event *ev;

    if (line_i == 0) {
        new_indent = 0;
    } else {
        new_indent = get_line_indent(buf, line_i - 1);
    }
    cur_indent = get_line_indent(buf, line_i);
    pos.line = line_i;
    pos.col = 0;
    if (new_indent > cur_indent) {
        line.n = new_indent - cur_indent;
        line.s = xmalloc(new_indent - cur_indent);
        memset(line.s, ' ', line.n);
        ev = insert_lines(buf, &pos, &line, 1, 1);
        free(line.s);
        return ev;
    }
    to.line = line_i;
    to.col = cur_indent - new_indent;
    return delete_range(buf, &pos, &to);
}

struct raw_line *get_lines(struct buf *buf, const struct pos *from,
        const struct pos *to, size_t *p_num_lines)
{
    struct raw_line *lines;
    size_t num_lines;

    if (from->line != to->line) {
        num_lines = to->line - from->line + 1;
        lines = xreallocarray(NULL, num_lines, sizeof(*lines));
        init_raw_line(&lines[0], &buf->lines[from->line].s[from->col],
                buf->lines[from->line].n - from->col);

        for (size_t i = from->line + 1; i < to->line; i++) {
            init_raw_line(&lines[i - from->line], &buf->lines[i].s[0],
                    buf->lines[i].n);
        }
        if (to->line != buf->num_lines) {
            init_raw_line(&lines[num_lines - 1], buf->lines[to->line].s, to->col);
        } else {
            init_raw_line(&lines[num_lines - 1], NULL, 0);
        }
    } else {
        num_lines = 1;
        lines = xmalloc(sizeof(*lines));
        init_raw_line(&lines[0], &buf->lines[from->line].s[from->col],
                to->col - from->col);
    }
    *p_num_lines = num_lines;
    return lines;
}

struct undo_event *delete_range(struct buf *buf, const struct pos *pfrom,
        const struct pos *pto)
{
    struct pos from, to;
    struct undo_event ev, *p_ev;

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
    ev.flags = IS_DELETION;
    ev.pos = from;
    ev.lines = get_lines(buf, &from, &to, &ev.num_lines);
    p_ev = add_event(buf, &ev);

    _delete_range(buf, &from, &to);
    return p_ev;
}

void _delete_range(struct buf *buf, const struct pos *pfrom, const struct pos *pto)
{
    struct pos from, to;
    struct line *fl, *tl;

    from = *pfrom;
    to = *pto;

    update_dirty_lines(buf, from.line, from.line == to.line ? from.line :
            from.line + 1);

    fl = &buf->lines[from.line];
    if (from.line == to.line) {
        fl->n -= to.col;
        memmove(&fl->s[from.col], &fl->s[to.col], fl->n);
        fl->n += from.col;
        mark_dirty(fl);
    } else if (to.line >= buf->num_lines) {
        /* trim the line */
        fl->n = from.col;
        if (from.line > 0) {
            from.line--;
        }
        /* delete the remaining lines */
        for (size_t i = from.line + 1; i < buf->num_lines; i++) {
            clear_line(&buf->lines[i]);
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
            clear_line(&buf->lines[i]);
        }
        memmove(&fl[1], &tl[1], sizeof(*buf->lines) *
                (buf->num_lines - to.line - 1));
        buf->num_lines -= to.line - from.line;
        mark_dirty(fl);
    }
}

struct undo_event *delete_block(struct buf *buf, const struct pos *pfrom,
        const struct pos *pto)
{
    struct pos from, to;
    struct line *line;
    size_t to_col;
    struct undo_event ev, *first_ev = NULL, *p_ev;

    from = *pfrom;
    to = *pto;

    sort_block_positions(&from, &to);

    if (from.line >= buf->num_lines) {
        return NULL;
    }

    to.line = MIN(to.line, buf->num_lines - 1);

    for (size_t i = from.line; i <= to.line; i++) {
        line = &buf->lines[i];
        if (from.col >= line->n) {
            continue;
        }
        to_col = MIN(to.col + 1, line->n);
        if (to_col == 0) {
            continue;
        }

        ev.flags = IS_DELETION | IS_TRANSIENT;
        ev.pos.line = i;
        ev.pos.col = from.col;
        ev.num_lines = 1;
        ev.lines = xmalloc(sizeof(*ev.lines));
        init_raw_line(&ev.lines[0], &line->s[from.col], to_col - from.col);
        p_ev = add_event(buf, &ev);
        if (first_ev == NULL) {
            first_ev = p_ev;
        }

        line->n -= to_col;
        memmove(&line->s[from.col], &line->s[to_col], line->n);
        line->n += from.col;

        mark_dirty(line);
    }

    if (first_ev == NULL) {
        return NULL;
    }

    update_dirty_lines(buf, from.line, to.line);

    p_ev->flags ^= IS_TRANSIENT;
    return first_ev;
}

struct undo_event *change_block(struct buf *buf, const struct pos *pfrom,
        const struct pos *pto, int (*conv)(int))
{
    struct pos from, to;
    struct line *line;
    size_t to_col;
    struct undo_event ev, *first_ev = NULL, *p_ev;
    char ch;

    from = *pfrom;
    to = *pto;

    sort_block_positions(&from, &to);

    if (from.line >= buf->num_lines) {
        return NULL;
    }

    to.line = MIN(to.line, buf->num_lines - 1);

    update_dirty_lines(buf, from.line, to.line);

    for (size_t i = from.line; i <= to.line; i++) {
        line = &buf->lines[i];
        if (from.col >= line->n) {
            continue;
        }
        to_col = MIN(to.col + 1, line->n);
        if (to_col == 0) {
            continue;
        }

        ev.flags = IS_REPLACE | IS_TRANSIENT;
        ev.pos.line = i;
        ev.pos.col = from.col;
        ev.num_lines = 1;
        ev.lines = xmalloc(sizeof(*ev.lines));
        init_raw_line(&ev.lines[0], &line->s[from.col], to_col - from.col);
        for (size_t i = from.col; i < to_col; i++) {
            ch = (*conv)(line->s[i]);
            ev.lines[0].s[i - from.col] ^= ch;
            line->s[i] = ch;
        }
        p_ev = add_event(buf, &ev);
        if (first_ev == NULL) {
            first_ev = p_ev;
        }

        mark_dirty(line);
    }

    if (first_ev == NULL) {
        return NULL;
    }

    p_ev->flags ^= IS_TRANSIENT;
    return first_ev;
}

struct undo_event *change_range(struct buf *buf, const struct pos *pfrom,
        const struct pos *pto, int (*conv)(int))
{
    struct pos from, to;
    struct undo_event ev;
    struct line *line;
    char ch;

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

    /* make sure there is text to change */

    /* there can be nothing to replace when the cursor is at the end of a
     * line, so this moves `from` as high up as possible where as the latter
     * moves `to` as much down as possible
     */
    while (from.line < to.line && from.col == buf->lines[from.line].n) {
        from.line++;
        from.col = 0;
    }
    while (to.col == 0 && to.line > from.line) {
        to.line--;
        to.col = buf->lines[to.line].n;
    }

    if (from.line == to.line && from.col == to.col) {
        return 0;
    }

    update_dirty_lines(buf, from.line, to.line);

    ev.flags = IS_REPLACE;
    ev.pos = from;
    if (from.line != to.line) {
        ev.num_lines = to.line - from.line + 1;
        ev.lines = xreallocarray(NULL, ev.num_lines, sizeof(*ev.lines));

        line = &buf->lines[from.line];
        init_raw_line(&ev.lines[0], &line->s[from.col], line->n - from.col);
        for (size_t i = from.col; i < line->n; i++) {
            ch = (*conv)(line->s[i]);
            ev.lines[0].s[i - from.col] ^= ch;
            line->s[i] = ch;
        }
        mark_dirty(line);

        for (size_t i = from.line + 1; i < to.line; i++) {
            line = &buf->lines[i];
            init_raw_line(&ev.lines[i - from.line], &line->s[0], line->n);
            for (size_t j = 0; j < line->n; j++) {
                ch = (*conv)(line->s[j]);
                ev.lines[i - from.line].s[j] ^= ch;
                line->s[j] = ch;
            }
            mark_dirty(line);
        }

        line = &buf->lines[to.line];
        init_raw_line(&ev.lines[to.line - from.line], &line->s[0], to.col);
        for (size_t i = 0; i < to.col; i++) {
            ch = (*conv)(line->s[i]);
            ev.lines[to.line - from.line].s[i] ^= ch;
            line->s[i] = ch;
        }
        mark_dirty(line);
    } else {
        ev.num_lines = 1;
        ev.lines = xmalloc(sizeof(*ev.lines));
        line = &buf->lines[from.line];
        init_raw_line(&ev.lines[0], &line->s[from.col], to.col - from.col);
        for (size_t i = from.col; i < to.col; i++) {
            ch = (*conv)(line->s[i]);
            ev.lines[0].s[i - from.col] ^= ch;
            line->s[i] = ch;
        }
        mark_dirty(line);
    }

    return add_event(buf, &ev);
}
