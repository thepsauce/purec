#include "buf.h"
#include "util.h"
#include "xalloc.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <ncurses.h>

/* TODO: buffer list */

int write_file(struct buf *buf, const char *file)
{
    struct stat st;
    FILE *fp;
    struct line *line;
    size_t num_bytes = 0;

    if (file == NULL) {
        if (buf->path == NULL) {
            format_message("no file name");
            return -1;
        }
        file = buf->path;
        if (stat(file, &st) == 0) {
            if (st.st_mtime != buf->st.st_mtime) {
                format_message("file changed, use `:w!`");
                return -1;
            }
        }
    } else if (buf->path == NULL) {
        buf->path = xstrdup(file);
        file = buf->path;
    }

    fp = fopen(file, "w");
    if (fp == NULL) {
        format_message("could not open '%s': %s", file, strerror(errno));
        return -1;
    }

    for (size_t i = 0; i < buf->num_lines; i++) {
        line = &buf->lines[i];
        fwrite(line->s, 1, line->n, fp);
        num_bytes += line->n;
        if (i + 1 != buf->num_lines || line->n != 0) {
            fputc('\n', fp);
            num_bytes++;
        }
    }

    fclose(fp);

    if (file == buf->path) {
        stat(buf->path, &buf->st);
        buf->save_event_i = buf->event_i;
    }
    format_message("%s %zuL, %zuB written", buf->path, buf->num_lines,
            num_bytes);
    return 0;
}

struct undo_event *read_file(struct buf *buf, const struct pos *pos,
        const char *file)
{
    FILE *fp;
    struct raw_line *lines = NULL;
    size_t a_lines = 0;
    size_t num_lines = 0;
    char *s_line = NULL;
    size_t a_line = 0;
    ssize_t line_len;
    struct undo_event *ev;

    fp = fopen(file, "r");
    if (fp == NULL) {
        format_message("failed opening '%s': %s\n", file, strerror(errno));
        return NULL;
    }

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
    fclose(fp);

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
        format_message("failed opening '%s': %s\n", buf->path, strerror(errno));
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
        clear_line(&buf->lines[i]);
    }
    free(buf->lines);
    for (size_t i = 0; i < buf->num_events; i++) {
        free_event(buf->events[i]);
    }
    free(buf->events);
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
    const struct raw_line *c_last;
    size_t num_lines;

    if (c_num_lines == 0 || repeat == 0) {
        return NULL;
    }

    /* duplicate the lines */
    num_lines = 1 + (c_num_lines - 1) * repeat;
    lines = xreallocarray(NULL, num_lines, sizeof(*lines));
    line = &lines[0];
    c_last = &c_lines[c_num_lines - 1];
    if (c_num_lines == 1) {
        line->n = c_last->n * repeat;
        line->s = xmalloc(c_last->n * repeat);
        for (size_t r = 0; r < repeat; r++) {
            memcpy(&line->s[r * c_last->n], &c_last->s[0], c_last->n);
        }
    } else {
        for (size_t r = 0;;) {
            for (size_t i = 0; i < c_num_lines; i++) {
                init_raw_line(line, &c_lines[i].s[0], c_lines[i].n);
                line++;
            }
            r++;
            if (r >= repeat) {
                break;
            }
            line[-1].s = xrealloc(line[-1].s, line[-1].n + c_last->n);
            memcpy(&line[-1].s[line[-1].n], &c_last->s[0], c_last->n);
            lines[-1].n += c_last->n;
        }
    }

    _insert_lines(buf, pos, lines, num_lines);

    ev.flags = IS_INSERTION;
    ev.pos = *pos;
    ev.lines = lines;
    ev.num_lines = num_lines;
    return add_event(buf, &ev);
}

void _insert_lines(struct buf *buf, const struct pos *pos,
        const struct raw_line *lines, size_t num_lines)
{
    const struct raw_line *raw_line;
    struct line *line, *at_line;
    size_t old_n;

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
        return insert_lines(buf, &pos, &line, 1, 1);
    }
    to.line = line_i;
    to.col = cur_indent - new_indent;
    return delete_range(buf, &pos, &to);
}

struct undo_event *delete_range(struct buf *buf, const struct pos *pfrom,
        const struct pos *pto)
{
    struct pos from, to;
    struct undo_event ev, *pev;
    struct raw_line *lines;
    size_t num_lines;

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
    if (from.line != to.line) {
        num_lines = to.line - from.line + 1;
        lines = xreallocarray(NULL, num_lines, sizeof(*lines));
        init_raw_line(&lines[0], &buf->lines[from.line].s[from.col],
                buf->lines[from.line].n - from.col);

        for (size_t i = from.line + 1; i < to.line; i++) {
            init_raw_line(&lines[i - from.line], &buf->lines[i].s[0],
                    buf->lines[i].n);
        }
        if (to.line != buf->num_lines) {
            init_raw_line(&lines[num_lines - 1], buf->lines[to.line].s, to.col);
        } else {
            init_raw_line(&lines[num_lines - 1], NULL, 0);
        }
    } else {
        num_lines = 1;
        lines = xmalloc(sizeof(*lines));
        init_raw_line(&lines[0], &buf->lines[from.line].s[from.col],
                to.col - from.col);
    }
    ev.lines = lines;
    ev.num_lines = num_lines;
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
    struct undo_event ev, *first_ev = NULL, *pev;

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
        pev = add_event(buf, &ev);
        if (first_ev == NULL) {
            first_ev = pev;
        }

        line->n -= to_col;
        memmove(&line->s[from.col], &line->s[to_col], line->n);
        line->n += from.col;

        mark_dirty(line);
    }

    if (first_ev == NULL) {
        return NULL;
    }

    pev->flags ^= IS_TRANSIENT;
    return first_ev;
}

struct undo_event *change_block(struct buf *buf, const struct pos *pfrom,
        const struct pos *pto, int (*conv)(int))
{
    struct pos from, to;
    struct line *line;
    size_t to_col;
    struct undo_event ev, *first_ev = NULL, *pev;
    char ch;

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
        pev = add_event(buf, &ev);
        if (first_ev == NULL) {
            first_ev = pev;
        }

        mark_dirty(line);
    }

    if (first_ev == NULL) {
        return NULL;
    }

    pev->flags ^= IS_TRANSIENT;
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
