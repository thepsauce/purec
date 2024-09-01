#include "lang.h"
#include "xalloc.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

struct buf *FirstBuffer;

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
    buf->path = rel_path;
    init_load_buffer(buf);

    /* add buffer to linked list */
    if (FirstBuffer == NULL) {
        FirstBuffer = buf;
        buf->id = 1;
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

/**
 * Finds given string within a null terminated string list.
 *
 * `s_list` is a null terminated string list, the end is marked by a double null
 * terminator. Only if the given string begins with any of the strings in `s_list`,
 * true is returned.
 *
 * @param s_list    The list of strings.
 * @param s         The string to check.
 * @param s_len     The length of the string to theck.
 *
 * @return Whether the string was found.
 */
static bool has_prefix(const char *s_list, const char *s, size_t s_len)
{
    for (size_t i = 0; i < s_len; ) {
        if (s_list[i] != s[i]) {
            if (s_list[i] == '\0') {
                return true;
            }
            while (s_list[i] != '\0') {
                i++;
            }
            i++;
            if (s_list[i] == '\0') {
                return false;
            }
            /* jump to the next item */
            s_list = &s_list[i];
            i = 0;
        } else {
            i++;
        }
    }
    return true;
}

size_t detect_language(struct buf *buf)
{
    char *ext, *end;

    for (size_t i = 0, j; i < buf->num_lines; i++) {
        for (j = 0; j < buf->lines[i].n; j++) {
            if (isspace(buf->lines[i].s[j])) {
                continue;
            }

            if (buf->lines[i].s[j] == '#') {
                return C_LANG;
            }
            i = buf->num_lines - 1;
            break;
        }
    }

    if (buf->path == NULL) {
        return NO_LANG;
    }

    end = buf->path + strlen(buf->path);

    for (ext = end; ext > buf->path; ext--) {
        if (ext[-1] == '.' || ext[-1] == '/') {
            break;
        }
    }

    if (end == ext) {
        return NO_LANG;
    }

    for (int l = 1; l < NUM_LANGS; l++) {
        if (has_prefix(Langs[l].file_exts, ext, end - ext)) {
            return l;
        }
    }
    return NO_LANG;
}

void init_load_buffer(struct buf *buf)
{
    FILE *fp;
    char *s_line = NULL;
    size_t a_line = 0;
    ssize_t line_len;
    struct line *line;

    if (buf->path == NULL || (fp = fopen(buf->path, "r")) == NULL) {
        buf->a_lines = 1;
        buf->lines = xcalloc(buf->a_lines, sizeof(*buf->lines));
        buf->num_lines = 1;
        buf->lang = detect_language(buf);
        return;
    }

    (void) stat(buf->path, &buf->st);

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

    free(s_line);

    if (buf->num_lines == 0) {
        /* the file was completely empty */
        buf->a_lines = 1;
        buf->lines = xcalloc(buf->a_lines, sizeof(*buf->lines));
        buf->num_lines = 1;
    } else {
        buf->max_dirty_i = buf->num_lines - 1;
    }

    buf->lang = detect_language(buf);
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
        free(buf->events[i]);
    }
    free(buf->events);
    free(buf->matches);
    free(buf->search_pat);

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

struct buf *get_buffer(size_t id)
{
    struct buf *buf;

    for (buf = FirstBuffer; buf != NULL; buf = buf->next) {
        if (buf->id == id) {
            return buf;
        }
    }
    return NULL;
}

size_t get_buffer_count(void)
{
    size_t c = 0;

    for (struct buf *buf = FirstBuffer; buf != NULL; buf = buf->next) {
        c++;
    }
    return c;
}

void set_language(struct buf *buf, size_t lang)
{
    if (buf->lang == lang) {
        return;
    }
    buf->min_dirty_i = 0;
    buf->max_dirty_i = buf->num_lines - 1;
    for (size_t i = 0; i < buf->num_lines; i++) {
        mark_dirty(&buf->lines[i]);
    }
    buf->lang = lang;
}

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

    return add_event(buf, IS_INSERTION, pos, lines, num_lines);
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

struct undo_event *insert_block(struct buf *buf, const struct pos *pos,
        const struct raw_line *c_lines, size_t c_num_lines, size_t repeat)
{
    struct raw_line *lines, *line;
    size_t num_lines;
    size_t sp;

    /* duplicate and repeat the lines, add padding to the front of some lines */
    num_lines = 1 + (c_num_lines - 1) * repeat;
    lines = xreallocarray(NULL, num_lines, sizeof(*lines));
    line = &lines[0];
    for (size_t i = 0; i < c_num_lines; i++) {
        if (pos->col > buf->lines[pos->line + i].n) {
            sp = pos->col - buf->lines[pos->line + i].n;
        } else {
            sp = 0;
        }
        line->n = c_lines[i].n * repeat;
        line->s = xmalloc(sp + line->n);
        memset(&line->s[0], ' ', sp);
        for (size_t r = 0; r < repeat; r++) {
            memcpy(&line->s[sp + r * c_lines->n], &c_lines->s[i],
                    c_lines[i].n);
        }
        line++;
    }

    _insert_block(buf, pos, lines, num_lines);

    return add_event(buf, IS_BLOCK | IS_INSERTION, pos, lines, num_lines);
}

void _insert_block(struct buf *buf, const struct pos *pos,
        const struct raw_line *lines, size_t num_lines)
{
    const struct raw_line *rl;
    struct line *line;
    size_t col;

    update_dirty_lines(buf, pos->line, pos->line + num_lines - 1);

    for (size_t i = 0; i < num_lines; i++) {
        line = &buf->lines[pos->line + i];
        rl = &lines[i];
        col = MIN(pos->col, line->n);

        line->s = xrealloc(line->s, line->n + rl->n);
        memmove(&line->s[col + rl->n], &line->s[col], line->n - col);
        memcpy(&line->s[col], &rl->s[0], rl->n);
        line->n += rl->n;

        mark_dirty(line);
    }
}

struct undo_event *break_line(struct buf *buf, const struct pos *pos)
{
    struct line *line, *at_line;
    struct raw_line *lines;

    update_dirty_lines(buf, pos->line, pos->line + 1);

    line = grow_lines(buf, pos->line + 1, 1);
    at_line = &buf->lines[pos->line];
    line->flags = 0;
    line->state = 0;
    line->attribs = NULL;
    line->n = at_line->n - pos->col;
    line->s = xmemdup(&at_line->s[pos->col], line->n);
    at_line->n = pos->col;

    mark_dirty(at_line);

    lines = xcalloc(2, sizeof(*lines));
    return add_event(buf, IS_INSERTION, pos, lines, 2);
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

struct raw_line *get_block(struct buf *buf, const struct pos *from,
        const struct pos *to, size_t *p_num_lines)
{
    struct raw_line *lines;
    size_t num_lines;
    struct line *line;
    size_t to_col;

    num_lines = to->line - from->line + 1;
    lines = xreallocarray(NULL, num_lines, sizeof(*lines));
    for (size_t i = from->line; i <= to->line; i++) {
        line = &buf->lines[i];
        if (from->col >= line->n) {
            init_raw_line(&lines[i - from->line], NULL, 0);
            continue;
        }
        to_col = MIN(to->col + 1, line->n);
        if (to_col == 0) {
            init_raw_line(&lines[i - from->line], NULL, 0);
            continue;
        }
        init_raw_line(&lines[i - from->line], &line->s[from->col],
                to_col - from->col);
    }
    *p_num_lines = num_lines;
    return lines;
}

struct undo_event *delete_range(struct buf *buf, const struct pos *pfrom,
        const struct pos *pto)
{
    struct pos from, to;
    struct undo_event *p_ev;
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
    lines = get_lines(buf, &from, &to, &num_lines);
    p_ev = add_event(buf, IS_DELETION, &from, lines, num_lines);

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
    struct undo_event *ev;
    struct raw_line *lines;
    size_t num_lines;

    from = *pfrom;
    to = *pto;

    sort_block_positions(&from, &to);

    if (from.line >= buf->num_lines) {
        return NULL;
    }

    to.line = MIN(to.line, buf->num_lines - 1);

    num_lines = to.line - from.line + 1;
    lines = xreallocarray(NULL, num_lines, sizeof(*lines));
    for (size_t i = from.line; i <= to.line; i++) {
        line = &buf->lines[i];
        if (from.col >= line->n) {
            init_raw_line(&lines[i - from.line], NULL, 0);
            continue;
        }
        to_col = MIN(to.col + 1, line->n);
        if (to_col == 0) {
            init_raw_line(&lines[i - from.line], NULL, 0);
            continue;
        }
        init_raw_line(&lines[i - from.line], &line->s[from.col],
                to_col - from.col);
    }

    ev = add_event(buf, IS_BLOCK | IS_DELETION, &from, lines, num_lines);

    _delete_block(buf, &from, &to);

    return ev;
}

void _delete_block(struct buf *buf, const struct pos *from,
        const struct pos *to)
{
    struct line *line;
    size_t to_col;

    update_dirty_lines(buf, from->line, to->line);

    for (size_t i = from->line; i <= to->line; i++) {
        line = &buf->lines[i];
        if (from->col >= line->n) {
            continue;
        }
        to_col = MIN(to->col + 1, line->n);
        if (to_col == 0) {
            continue;
        }

        line->n -= to_col;
        memmove(&line->s[from->col], &line->s[to_col], line->n);
        line->n += from->col;

        mark_dirty(line);
    }
}

struct undo_event *change_block(struct buf *buf, const struct pos *pfrom,
        const struct pos *pto, int (*conv)(int))
{
    struct pos from, to;
    struct line *line;
    size_t to_col;
    struct undo_event *first_ev = NULL, *p_ev;
    struct pos pos;
    struct raw_line *lines;
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

        pos.line = i;
        pos.col = from.col;
        lines = xmalloc(sizeof(*lines));
        init_raw_line(&lines[0], &line->s[from.col], to_col - from.col);
        for (size_t i = from.col; i < to_col; i++) {
            ch = (*conv)(line->s[i]);
            lines[0].s[i - from.col] ^= ch;
            line->s[i] = ch;
        }
        p_ev = add_event(buf, IS_REPLACE | IS_TRANSIENT, &pos, lines, 1);
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
    struct raw_line *lines;
    size_t num_lines;
    struct line *line;
    char ch;

    from = *pfrom;
    to = *pto;

    /* clip lines */
    if (from.line >= buf->num_lines) {
        from.line = buf->num_lines - 1;
    }
    /* to is allowed to go out of bounds here */
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

    if (from.line != to.line) {
        num_lines = to.line - from.line + 1;
        lines = xreallocarray(NULL, num_lines, sizeof(*lines));

        line = &buf->lines[from.line];
        init_raw_line(&lines[0], &line->s[from.col], line->n - from.col);
        for (size_t i = from.col; i < line->n; i++) {
            ch = (*conv)(line->s[i]);
            lines[0].s[i - from.col] ^= ch;
            line->s[i] = ch;
        }
        mark_dirty(line);

        for (size_t i = from.line + 1; i < to.line; i++) {
            line = &buf->lines[i];
            init_raw_line(&lines[i - from.line], &line->s[0], line->n);
            for (size_t j = 0; j < line->n; j++) {
                ch = (*conv)(line->s[j]);
                lines[i - from.line].s[j] ^= ch;
                line->s[j] = ch;
            }
            mark_dirty(line);
        }

        line = &buf->lines[to.line];
        init_raw_line(&lines[to.line - from.line], &line->s[0], to.col);
        for (size_t i = 0; i < to.col; i++) {
            ch = (*conv)(line->s[i]);
            lines[to.line - from.line].s[i] ^= ch;
            line->s[i] = ch;
        }
        mark_dirty(line);
    } else {
        num_lines = 1;
        lines = xmalloc(sizeof(*lines));
        line = &buf->lines[from.line];
        init_raw_line(&lines[0], &line->s[from.col], to.col - from.col);
        for (size_t i = from.col; i < to.col; i++) {
            ch = (*conv)(line->s[i]);
            lines[0].s[i - from.col] ^= ch;
            line->s[i] = ch;
        }
        mark_dirty(line);
    }

    return add_event(buf, IS_REPLACE, &from, lines, num_lines);
}

size_t search_string(struct buf *buf, const char *s)
{
    size_t l;
    struct line *line;

    struct match *matches;
    size_t n;

    free(buf->search_pat);
    buf->search_pat = xstrdup(s);

    l = strlen(s);
    if (l == 0) {
        free(buf->matches);
        buf->matches = NULL;
        buf->num_matches = 0;
        return 0;
    }

    matches = NULL;
    n = 0;
    for (size_t i = 0; i < buf->num_lines; i++) {
        line = &buf->lines[i];
        for (size_t j = 0; j + l < line->n; j++) {
            if (memcmp(&line->s[j], s, l) != 0) {
                continue;
            }
            matches = xreallocarray(matches, n + 1, sizeof(*matches));
            matches[n].from.line = i;
            matches[n].from.col = j;
            matches[n].to.line = i;
            matches[n].to.col = j + l;
            n++;
        }
    }
    free(buf->matches);
    buf->matches = matches;
    buf->num_matches = n;
    return n;
}
