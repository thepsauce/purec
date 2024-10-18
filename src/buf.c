#include "color.h"
#include "fuzzy.h"
#include "lang.h"
#include "xalloc.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

struct buf *FirstBuffer;

struct buf *create_buffer(const char *path)
{
    char            *abs_path;
    struct buf      *buf, *prev;

    /* check if a buffer with that path exists already */
    if (path != NULL) {
        abs_path = get_absolute_path(path);
        for (buf = FirstBuffer; buf != NULL; buf = buf->next) {
            if (buf->path == NULL) {
                continue;
            }
            if (strcmp(buf->path, abs_path) == 0) {
                free(abs_path);
                return buf;
            }
        }
    } else {
        abs_path = NULL;
    }

    buf = xcalloc(1, sizeof(*buf));
    buf->path = abs_path;
    (void) init_load_buffer(buf);
    /* -1 so +1 does not wrap around */
    buf->ev_last_indent = SIZE_MAX - 1;

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
 * terminator. Only if the given string is equal to any of the strings in `s_list`,
 * or the first mismatching character is an asterisk, then true is returned.
 *
 * @param s_list    The list of pattern strings.
 * @param s         The string to check.
 * @param s_len     The length of the string to theck.
 *
 * @return Whether the string was found.
 */
static bool find_string_in_list(const char *s_list, const char *s, size_t s_len)
{
    size_t              len;

    while (s_list[0] != '\0') {
        len = strlen(s_list);
        if (match_pattern_exact(s, s_len, s_list)) {
            return true;
        }
        s_list = &s_list[len + 1];
    }
    return false;
}

size_t detect_language(struct buf *buf)
{
    static const char   *commit_detect = "# Please enter the commit message";
    line_t              i;
    col_t               j;
    size_t              len;
    int                 l;
    char                *name;

    if (buf->num_lines > 4) {
        if (buf->lines[0].n == 0) {
            if (buf->lines[1].n > (col_t) strlen(commit_detect) &&
                    memcmp(buf->lines[1].s, commit_detect,
                           strlen(commit_detect)) == 0) {
                return COMMIT_LANG;
            }
        }
    }
    for (i = 0; i < buf->num_lines; i++) {
        for (j = 0; j < buf->lines[i].n; j++) {
            if (isspace(buf->lines[i].s[j])) {
                continue;
            }

            if (buf->lines[i].s[j] == '#') {
                if (j + 1 == buf->lines[i].n ||
                        buf->lines[i].s[j + 1] != ' ') {
                    return C_LANG;
                }
            }
            i = buf->num_lines - 1;
            break;
        }
    }

    if (buf->path == NULL) {
        return NO_LANG;
    }

    name = strrchr(buf->path, '/');
    if (name == NULL) {
        name = buf->path;
    } else {
        name++;
    }
    len = strlen(name);
    for (l = 1; l < NUM_LANGS; l++) {
        if (find_string_in_list(Langs[l].file_exts, name, len)) {
            return l;
        }
    }
    return NO_LANG;
}

int init_load_buffer(struct buf *buf)
{
    FILE            *fp;
    char            *s_line;
    size_t          a_line;
    ssize_t         line_len;
    struct line     *line;

beg:
    if (buf->path == NULL || (fp = fopen(buf->path, "r")) == NULL) {
        buf->a_lines = 1;
        buf->lines = xcalloc(buf->a_lines, sizeof(*buf->lines));
        buf->num_lines = 1;
        /* this will detect the language based on the file extension */
        buf->lang = detect_language(buf);
        return 1;
    }

    if (stat(buf->path, &buf->st) == 0 && (buf->st.st_mode & S_IFDIR)) {
        s_line = buf->path;
        buf->path = choose_file(buf->path);
        if (buf->path != NULL) {
            buf->path = get_absolute_path(buf->path);
        }
        free(s_line);
        goto beg;
    }

    s_line = NULL;
    a_line = 0;
    while (line_len = getline(&s_line, &a_line, fp), line_len > 0) {
        for (; line_len > 0; line_len--) {
            if (s_line[line_len - 1] != '\n' &&
                    s_line[line_len - 1] != '\r') {
                break;
            }
        }
        line = grow_lines(buf, buf->num_lines, 1);
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
    }

    set_language(buf, detect_language(buf));
    return 0;
}

void destroy_buffer(struct buf *buf)
{
    line_t          i;
    struct buf      *prev;

    free(buf->path);
    for (i = 0; i < buf->num_lines; i++) {
        clear_line(&buf->lines[i]);
    }
    free(buf->lines);
    free(buf->events);
    free(buf->parens);
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

char *get_pretty_path(const char *path)
{
    static char     *s;
    static size_t   s_a;

    size_t          path_len;
    char            *cwd;
    size_t          cwd_len;
    const char      *home;
    size_t          home_len;

    if (path == NULL) {
        if (s_a < sizeof("[No name]")) {
            s_a = sizeof("[No name]");
            s = xrealloc(s, s_a);
        }
        return strcpy(s, "[No name]");
    }

    path_len = strlen(path);
    /* +4 for safety */
    if (s_a + 4 < path_len) {
        s_a = path_len + 4;
        s = xrealloc(s, s_a);
    }

    cwd = getcwd(NULL, 0);
    if (cwd != NULL) {
        cwd_len = strlen(cwd);
    } else {
        cwd_len = 0;
    }

    home = getenv("HOME");
    if (home == NULL && cwd == NULL) {
        return strcpy(s, path);
    }

    if (home != NULL) {
        home_len = strlen(home);
    } else {
        home_len = 0;
    }
    if (home_len > cwd_len && strncmp(path, home, home_len) == 0) {
        s[0] = '~';
        strcpy(&s[1], &path[home_len]);
    } else if (strncmp(path, cwd, cwd_len) == 0) {
        if (path[cwd_len] == '\0') {
            s[0] = '.';
            s[1] = '\0';
        } else {
            strcpy(s, &path[cwd_len + 1]);
        }
    } else if (home_len > 0 && strncmp(path, home, home_len) == 0) {
        s[0] = '~';
        strcpy(&s[1], &path[home_len]);
    } else {
        strcpy(s, path);
    }
    free(cwd);
    return s;
}

struct buf *get_buffer(size_t id)
{
    struct buf      *buf;

    for (buf = FirstBuffer; buf != NULL; buf = buf->next) {
        if (buf->id == id) {
            return buf;
        }
    }
    return NULL;
}

size_t get_buffer_count(void)
{
    struct buf *buf;
    size_t      c = 0;

    for (buf = FirstBuffer; buf != NULL; buf = buf->next) {
        c++;
    }
    return c;
}

void set_language(struct buf *buf, size_t lang)
{
    line_t          i;

    buf->lang = lang;
    for (i = 0; i < buf->num_lines; i++) {
        rehighlight_line(buf, i);
    }
}

size_t write_file(struct buf *buf, line_t from, line_t to, FILE *fp)
{
    struct line     *line;
    size_t          num_bytes = 0;

    to = MIN(to, buf->num_lines - 1);

    if (from > to) {
        return 0;
    }

    from = MIN(from, buf->num_lines - 1);

    for (; from <= to; from++) {
        line = &buf->lines[from];
        fwrite(line->s, 1, line->n, fp);
        num_bytes += line->n;
        if (from + 1 != buf->num_lines || line->n != 0) {
            fputc('\n', fp);
            num_bytes++;
        }
    }
    return num_bytes;
}

struct undo_event *read_file(struct buf *buf, const struct pos *pos, FILE *fp)
{
    struct raw_line *lines = NULL;
    line_t          a_lines = 0;
    line_t          num_lines = 0;
    char            *s_line = NULL;
    size_t          a_line = 0;
    ssize_t         line_len;

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
        num_lines++;
    }

    free(s_line);

    return insert_lines(buf, pos, lines, num_lines, 1);
}

size_t get_line_indent(struct buf *buf, line_t line_i)
{
    col_t           col = 0;
    struct          line *line;

    line = &buf->lines[line_i];
    while (col < line->n && isblank(line->s[col])) {
        col++;
    }
    return col;
}

struct undo_event *insert_lines(struct buf *buf, const struct pos *pos,
        const struct raw_line *c_lines, line_t c_num_lines, line_t repeat)
{
    struct raw_line *lines, *line;
    line_t          num_lines;
    line_t          r;
    line_t          i;

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
        for (r = 0; r < repeat; r++) {
            memcpy(&line->s[r * c_lines->n], &c_lines->s[0], c_lines->n);
        }
    } else {
        init_raw_line(line, &c_lines[0].s[0], c_lines[0].n);
        line++;
        r = 0;
        while (1) {
            for (i = 1; i < c_num_lines; i++) {
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

void _insert_lines(struct buf *buf, const struct pos *pos,
        const struct raw_line *lines, line_t num_lines)
{
    const struct raw_line   *raw_line;
    struct line             *line, *at_line;
    line_t                  i;
    col_t                   old_n;
    bool                    r;

    if (num_lines == 1) {
        raw_line = &lines[0];
        line = &buf->lines[pos->line];
        line->s = xrealloc(line->s, line->n + raw_line->n);
        memmove(&line->s[pos->col + raw_line->n], &line->s[pos->col],
                line->n - pos->col);
        memcpy(&line->s[pos->col], &raw_line->s[0], raw_line->n);
        line->n += raw_line->n;
        r = rehighlight_line(buf, pos->line);
        i = pos->line;
        while (r) {
            i++;
            r = rehighlight_line(buf, i);
        }
        return;
    }

    line = grow_lines(buf, pos->line + 1, num_lines - 1);
    for (i = 1; i < num_lines; i++) {
        raw_line = &lines[i];
        line->n = raw_line->n;
        line->s = xmalloc(line->n);
        memcpy(&line->s[0], &raw_line->s[0], raw_line->n);
        line++;
    }

    at_line = &buf->lines[pos->line];
    raw_line = &lines[num_lines - 1];

    /* add the end of the first line to the end of the last line */
    line--; /* `line` overshoots the last line, so decrement it */
    old_n = line->n;
    line->n += at_line->n - pos->col;
    line->s = xrealloc(line->s, line->n);
    memcpy(&line->s[old_n], &at_line->s[pos->col], at_line->n - pos->col);

    /* trim first line and insert first text segment */
    at_line->n = pos->col + lines[0].n;
    at_line->s = xrealloc(at_line->s, at_line->n);
    memcpy(&at_line->s[pos->col], &lines[0].s[0], lines[0].n);

    r = false;
    for (i = 0; i < num_lines; i++) {
        r = rehighlight_line(buf, pos->line + i);
    }
    while (r) {
        r = rehighlight_line(buf, pos->line + i);
        i++;
    }
}

struct undo_event *insert_block(struct buf *buf, const struct pos *pos,
        const struct raw_line *c_lines, line_t c_num_lines, line_t repeat)
{
    struct raw_line *lines, *line;
    line_t          num_lines;
    line_t          i, r;

    /* duplicate and repeat the lines */
    num_lines = 1 + (c_num_lines - 1) * repeat;
    lines = xreallocarray(NULL, num_lines, sizeof(*lines));
    line = &lines[0];
    for (i = 0; i < c_num_lines; i++) {
        line->n = c_lines[i].n * repeat;
        line->s = xmalloc(line->n);
        for (r = 0; r < repeat; r++) {
            memcpy(&line->s[r * c_lines->n], c_lines[i].s, c_lines[i].n);
        }
        line++;
    }

    _insert_block(buf, pos, lines, num_lines);

    return add_event(buf, IS_BLOCK | IS_INSERTION, pos, lines, num_lines);
}

void _insert_block(struct buf *buf, const struct pos *pos,
        const struct raw_line *lines, line_t num_lines)
{
    line_t                  i;
    const struct raw_line   *rl;
    struct line             *line;
    line_t                  col;
    bool                    r;

    r = false;
    for (i = 0; i < num_lines; i++) {
        rl = &lines[i];
        line = &buf->lines[pos->line + i];
        col = pos->col;
        if (rl->n == 0 || col > line->n) {
            if (r) {
                r = rehighlight_line(buf, pos->line + i);
            }
            continue;
        }

        line->s = xrealloc(line->s, line->n + rl->n);
        memmove(&line->s[col + rl->n], &line->s[col], line->n - col);
        memcpy(&line->s[col], &rl->s[0], rl->n);
        line->n += rl->n;
        r = rehighlight_line(buf, pos->line + i);
    }
    while (r) {
        r = rehighlight_line(buf, pos->line + i);
        i++;
    }
}

struct undo_event *break_line(struct buf *buf, const struct pos *pos)
{
    struct line     *line, *at_line;
    struct raw_line *lines;
    col_t           indent;
    line_t          i;
    size_t          ev_i;
    struct pos      p;

    line = grow_lines(buf, pos->line + 1, 1);
    at_line = &buf->lines[pos->line];
    line->n = at_line->n - pos->col;
    line->s = xmemdup(&at_line->s[pos->col], line->n);
    at_line->n = pos->col;

    /* the indentor needs these to be highlighted */
    (void) rehighlight_line(buf, pos->line);
    (void) rehighlight_line(buf, pos->line + 1);

    indent = Langs[buf->lang].indentor(buf, pos->line + 1);
    line->s = xrealloc(line->s, line->n + indent);
    memmove(&line->s[indent], &line->s[0], line->n);
    memset(line->s, ' ', indent);
    line->n += indent;

    i = pos->line + 1;
    while (rehighlight_line(buf, i)) {
        i++;
    }

    lines = xmalloc(sizeof(*lines) * 2);
    lines[0].s = NULL;
    lines[0].n = 0;
    lines[1].s = NULL;
    lines[1].n = 0;
    /* use index to prevent base pointer change from breaking the event
     * pointer
     */
    ev_i = add_event(buf, IS_INSERTION, pos, lines, 2) - buf->events;

    if (indent > 0) {
        if (line->n == indent) {
            buf->ev_last_indent = buf->event_i;
        }
        lines = xmalloc(sizeof(*lines));
        lines[0].s = xmalloc(indent);
        lines[0].n = indent;
        memset(lines[0].s, ' ', indent);
        p.col = 0;
        p.line = pos->line + 1;
        (void) add_event(buf, IS_INSERTION, &p, lines, 1);
    }
    /* return by index because of the base pointer change potential */
    return &buf->events[ev_i];
}

/**
 * Gets the first index of the parenthesis on the given line.
 *
 * @param buf       The buffer to look for a parenthesis.
 * @param line_i    The line to look for.
 *
 * @return The index of the parenthesis.
 */
static size_t get_paren_line(const struct buf *buf, line_t line_i)
{
    size_t          l, m, r;
    struct paren    *p;

    l = 0;
    r = buf->num_parens;
    while (l < r) {
        m = (l + r) / 2;

        p = &buf->parens[m];
        if (p->pos.line < line_i) {
            l = m + 1;
        } else if (p->pos.line > line_i) {
            r = m;
        } else {
            for (; m > 0; m--) {
                if (buf->parens[m - 1].pos.line != line_i) {
                    return m;
                }
            }
            return m;
        }
    }
    return l;
}

size_t get_match_line(struct buf *buf, line_t line_i)
{
    size_t          l, m, r;
    struct match    *match;

    l = 0;
    r = buf->num_matches;
    while (l < r) {
        m = (l + r) / 2;

        match = &buf->matches[m];
        if (match->from.line < line_i) {
            l = m + 1;
        } else if (match->from.line > line_i) {
            r = m;
        } else {
            for (; m > 0; m--) {
                if (buf->matches[m - 1].from.line != line_i) {
                    return m;
                }
            }
            return m;
        }
    }
    return l;
}

struct line *grow_lines(struct buf *buf, line_t line_i, line_t num_lines)
{
    size_t          index;

    if (buf->num_lines + num_lines > buf->a_lines) {
        buf->a_lines *= 2;
        buf->a_lines += num_lines;
        buf->lines = xreallocarray(buf->lines, buf->a_lines, sizeof(*buf->lines));
    }

    /* move tail of the line list to form a gap */
    memmove(&buf->lines[line_i + num_lines], &buf->lines[line_i],
            sizeof(*buf->lines) * (buf->num_lines - line_i));
    /* zero initialize the gap */
    memset(&buf->lines[line_i], 0, sizeof(*buf->lines) * num_lines);
    buf->num_lines += num_lines;

    index = get_paren_line(buf, line_i);
    for (; index < buf->num_parens; index++) {
        buf->parens[index].pos.line += num_lines;
    }

    index = get_match_line(buf, line_i);
    for (; index < buf->num_matches; index++) {
        buf->matches[index].from.line += num_lines;
        buf->matches[index].to  .line += num_lines;
    }

    return &buf->lines[line_i];
}

void remove_lines(struct buf *buf, line_t line_i, line_t num_lines)
{
    line_t          i;
    size_t          index;
    size_t          end;

    for (i = 0; i < num_lines; i++) {
        clear_line(&buf->lines[line_i + i]);
    }

    buf->num_lines -= num_lines;
    memmove(&buf->lines[line_i], &buf->lines[line_i + num_lines],
            sizeof(*buf->lines) * (buf->num_lines - line_i));

    index = get_paren_line(buf, line_i);
    for (end = index; end < buf->num_parens; end++) {
        if (buf->parens[end].pos.line >= line_i + num_lines) {
            break;
        }
    }
    buf->num_parens -= end - index;
    memmove(&buf->parens[index], &buf->parens[end],
            sizeof(*buf->parens) * (buf->num_parens - index));
    for (; index < buf->num_parens; index++) {
        buf->parens[index].pos.line -= num_lines;
    }

    index = get_match_line(buf, line_i);
    for (end = index; end < buf->num_matches; end++) {
        if (buf->matches[end].from.line >= line_i + num_lines) {
            break;
        }
    }
    buf->num_matches -= end - index;
    memmove(&buf->matches[index], &buf->matches[end],
            sizeof(*buf->matches) * (buf->num_matches - index));
    for (; index < buf->num_matches; index++) {
        buf->matches[index].from.line -= num_lines;
        buf->matches[index].to  .line -= num_lines;
    }

}

struct undo_event *indent_line(struct buf *buf, line_t line_i)
{
    col_t               cur_indent, new_indent;
    struct pos          pos, to;
    struct raw_line     line;
    struct undo_event   *ev;

    new_indent = Langs[buf->lang].indentor(buf, line_i);
    cur_indent = get_line_indent(buf, line_i);
    pos.line = line_i;
    pos.col = 0;
    if (cur_indent == buf->lines[line_i].n) {
        buf->ev_last_indent = buf->event_i;
    }
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
        const struct pos *to, line_t *p_num_lines)
{
    struct raw_line *lines;
    line_t          num_lines;
    line_t          i;

    if (from->line != to->line) {
        num_lines = to->line - from->line + 1;
        lines = xreallocarray(NULL, num_lines, sizeof(*lines));
        init_raw_line(&lines[0], &buf->lines[from->line].s[from->col],
                buf->lines[from->line].n - from->col);

        for (i = from->line + 1; i < to->line; i++) {
            init_raw_line(&lines[i - from->line], &buf->lines[i].s[0],
                    buf->lines[i].n);
        }
        if (to->line != buf->num_lines) {
            init_raw_line(&lines[num_lines - 1], buf->lines[to->line].s, to->col);
        } else {
            init_zero_raw_line(&lines[num_lines - 1]);
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
        const struct pos *to, line_t *p_num_lines)
{
    struct raw_line *lines;
    line_t          num_lines;
    line_t          i;
    struct line     *line;
    col_t           to_col;

    num_lines = to->line - from->line + 1;
    lines = xreallocarray(NULL, num_lines, sizeof(*lines));
    for (i = from->line; i <= to->line; i++) {
        line = &buf->lines[i];
        if (from->col >= line->n) {
            init_zero_raw_line(&lines[i - from->line]);
            continue;
        }
        to_col = MIN(to->col + 1, line->n);
        if (to_col == 0) {
            init_zero_raw_line(&lines[i - from->line]);
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
    struct pos          from, to;
    struct undo_event   *ev;
    struct raw_line     *lines;
    line_t              num_lines;

    from = *pfrom;
    to = *pto;

    /* swap if needed */
    sort_positions(&from, &to);

    /* clip lines */
    if (from.line >= buf->num_lines) {
        from.line = buf->num_lines - 1;
    }
    /* to is allowed to go out of bounds */
    if (to.line > buf->num_lines) {
        to.line = buf->num_lines;
    }

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
    ev = add_event(buf, IS_DELETION, &from, lines, num_lines);

    _delete_range(buf, &from, &to);
    return ev;
}

void _delete_range(struct buf *buf, const struct pos *pfrom, const struct pos *pto)
{
    struct pos      from, to;
    line_t          i;
    struct line     *fl, *tl;

    from = *pfrom;
    to = *pto;

    fl = &buf->lines[from.line];
    if (from.line == to.line) {
        fl->n -= to.col;
        memmove(&fl->s[from.col], &fl->s[to.col], fl->n);
        fl->n += from.col;
    } else if (to.line >= buf->num_lines) {
        /* trim the line */
        fl->n = from.col;
        if (from.line > 0) {
            from.line--;
        }
        /* delete the remaining lines */
        for (i = from.line + 1; i < buf->num_lines; i++) {
            clear_line(&buf->lines[i]);
        }
        remove_lines(buf, from.line + 1, to.line - from.line);
    } else {
        tl = &buf->lines[to.line];
        /* join the current line with the last line */
        fl->n = from.col + tl->n - to.col;
        fl->s = xrealloc(fl->s, fl->n);
        memcpy(&fl->s[from.col], &tl->s[to.col], tl->n - to.col);
        /* delete the remaining lines */
        remove_lines(buf, from.line + 1, to.line - from.line);
    }
    while (rehighlight_line(buf, from.line)) {
        from.line++;
    }
}

struct undo_event *delete_block(struct buf *buf, const struct pos *pfrom,
        const struct pos *pto)
{
    struct pos          from, to;
    struct line         *line;
    col_t               to_col;
    struct undo_event   *ev;
    struct raw_line     *lines;
    line_t              num_lines;
    line_t              i;

    from = *pfrom;
    to = *pto;

    sort_block_positions(&from, &to);

    if (from.line >= buf->num_lines) {
        return NULL;
    }

    to.line = MIN(to.line, buf->num_lines - 1);

    num_lines = to.line - from.line + 1;
    lines = xreallocarray(NULL, num_lines, sizeof(*lines));
    for (i = from.line; i <= to.line; i++) {
        line = &buf->lines[i];
        if (from.col >= line->n) {
            init_zero_raw_line(&lines[i - from.line]);
            continue;
        }
        to_col = MIN(to.col + 1, line->n);
        if (to_col == 0) {
            init_zero_raw_line(&lines[i - from.line]);
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
    bool            r;
    line_t          i;
    struct line     *line;
    col_t           to_col;

    r = false;
    for (i = from->line; i <= to->line; i++) {
        line = &buf->lines[i];
        if (from->col >= line->n) {
            if (r) {
                r = rehighlight_line(buf, i);
            }
            continue;
        }
        to_col = MIN(to->col + 1, line->n);
        if (to_col == 0) {
            if (r) {
                r = rehighlight_line(buf, i);
            }
            continue;
        }

        line->n -= to_col;
        memmove(&line->s[from->col], &line->s[to_col], line->n);
        line->n += from->col;
        if (!r) {
            r = rehighlight_line(buf, i);
        }
    }
    while (r) {
        r = rehighlight_line(buf, i);
        i++;
    }
}

struct undo_event *replace_lines(struct buf *buf, const struct pos *from,
                   const struct pos *to, const struct raw_line *lines,
                   line_t num_lines)
{
    struct undo_event   *ev;
    struct undo_event   *ev2;

    /* TODO: optimize this */
    ev = delete_range(buf, from, to);
    ev2 = insert_lines(buf, from, lines, num_lines, 1);

    return ev == NULL ? ev2 : ev2 == NULL ? ev : ev2 - 1;
}

int ConvChar;

int conv_to_char(int c)
{
    (void) c;
    return ConvChar;
}

struct undo_event *change_block(struct buf *buf, const struct pos *pfrom,
        const struct pos *pto, int (*conv)(int))
{
    struct pos          from, to;
    struct line         *line;
    col_t               to_col;
    struct undo_event   *ev;
    struct pos          pos;
    struct raw_line     *lines;
    char                ch;
    bool                r;
    line_t              i;
    col_t               j;

    from = *pfrom;
    to = *pto;

    sort_block_positions(&from, &to);

    if (from.line >= buf->num_lines) {
        return NULL;
    }

    to.line = MIN(to.line, buf->num_lines - 1);

    ev = NULL;
    r = false;
    for (i = from.line; i <= to.line; i++) {
        line = &buf->lines[i];
        if (from.col >= line->n) {
            if (r) {
                r = rehighlight_line(buf, i);
            }
            continue;
        }
        to_col = MIN(to.col + 1, line->n);
        if (to_col == 0) {
            if (r) {
                r = rehighlight_line(buf, i);
            }
            continue;
        }

        pos.line = line - buf->lines;
        pos.col = from.col;
        lines = xmalloc(sizeof(*lines));
        init_raw_line(&lines[0], &line->s[from.col], to_col - from.col);
        for (j = from.col; j < to_col; j++) {
            ch = (*conv)(line->s[j]);
            lines[0].s[j - from.col] ^= ch;
            line->s[j] = ch;
        }
        ev = add_event(buf, IS_REPLACE, &pos, lines, 1);
        r = rehighlight_line(buf, i);
    }

    return ev;
}

struct undo_event *change_range(struct buf *buf, const struct pos *pfrom,
        const struct pos *pto, int (*conv)(int))
{
    struct pos      from, to;
    struct raw_line *lines;
    line_t          num_lines;
    struct line     *line;
    char            ch;
    line_t          i;
    col_t           j;
    bool            r;

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

    if (from.line != to.line) {
        num_lines = to.line - from.line + 1;
        lines = xreallocarray(NULL, num_lines, sizeof(*lines));

        line = &buf->lines[from.line];
        init_raw_line(&lines[0], &line->s[from.col], line->n - from.col);
        for (i = from.col; i < line->n; i++) {
            ch = (*conv)(line->s[i]);
            lines[0].s[i - from.col] ^= ch;
            line->s[i] = ch;
        }

        for (i = from.line + 1; i < to.line; i++) {
            line = &buf->lines[i];
            init_raw_line(&lines[i - from.line], &line->s[0], line->n);
            for (j = 0; j < line->n; j++) {
                ch = (*conv)(line->s[j]);
                lines[i - from.line].s[j] ^= ch;
                line->s[j] = ch;
            }
        }

        line = &buf->lines[to.line];
        init_raw_line(&lines[to.line - from.line], &line->s[0], to.col);
        for (i = 0; i < to.col; i++) {
            ch = (*conv)(line->s[i]);
            lines[to.line - from.line].s[i] ^= ch;
            line->s[i] = ch;
        }
    } else {
        num_lines = 1;
        lines = xmalloc(sizeof(*lines));
        line = &buf->lines[from.line];
        init_raw_line(&lines[0], &line->s[from.col], to.col - from.col);
        for (i = from.col; i < to.col; i++) {
            ch = (*conv)(line->s[i]);
            lines[0].s[i - from.col] ^= ch;
            line->s[i] = ch;
        }
    }

    r = false;
    for (i = from.line; i <= to.line; i++) {
        r = rehighlight_line(buf, i);
    }
    while (r) {
        r = rehighlight_line(buf, i);
        i++;
    }

    return add_event(buf, IS_REPLACE, &from, lines, num_lines);
}

static struct match *search_line_pattern(struct buf *buf, line_t line_i,
                                         size_t *p_num_matches)
{
    struct line     *line;
    col_t           i;
    size_t          l;
    struct match    *matches;
    size_t          num_matches;

    line = &buf->lines[line_i];
    matches = NULL;
    num_matches = 0;
    for (i = 0; i < line->n; ) {
        l = match_pattern(line->s, i, line->n, buf->search_pat);
        if (l == 0) {
            i++;
            continue;
        }
        matches = xreallocarray(matches, num_matches + 1, sizeof(*matches));
        matches[num_matches].from.line = line_i;
        matches[num_matches].from.col = i;
        matches[num_matches].to.line = line_i;
        i += l;
        matches[num_matches].to.col = i;
        num_matches++;
    }
    *p_num_matches = num_matches;
    return matches;
}

static void fuse_matches(struct buf *buf, line_t line_i,
                         struct match *matches, size_t n)
{
    size_t          match_i, end;
    size_t          take;

    match_i = get_match_line(buf, line_i);
    end = match_i;
    while (end < buf->num_matches && buf->matches[end].from.line == line_i) {
        end++;
    }
    take = end - match_i;
    if (buf->num_matches + n - take > buf->a_matches) {
        buf->a_matches *= 2;
        buf->a_matches += n - take;
        buf->matches = xreallocarray(buf->matches, buf->a_matches,
                                     sizeof(*buf->matches));
    }
    memmove(&buf->matches[match_i + n],
            &buf->matches[match_i + take],
            sizeof(*buf->matches) * (buf->num_matches - match_i - take));
    memcpy(&buf->matches[match_i], matches, sizeof(*matches) * n);
    buf->num_matches += n - take;
}

size_t search_pattern(struct buf *buf, const char *pat)
{
    struct match    *matches;
    line_t          i;
    size_t          n;

    free(buf->search_pat);
    buf->search_pat = xstrdup(pat);

    matches = NULL;
    buf->num_matches = 0;
    for (i = 0; i < buf->num_lines; i++) {
        matches = search_line_pattern(buf, i, &n);
        fuse_matches(buf, i, matches, n);
        free(matches);
    }
    return buf->num_matches;
}

/**
 * Highlights a line with syntax highlighting.
 *
 * @param buf       The buffer containing the line.
 * @param line_i    The index of the line to highlight.
 * @param state     The starting state.
 */
static void highlight_line(struct buf *buf, size_t line_i, unsigned state)
{
    struct state_ctx    ctx;
    col_t               n;
    struct line         *line;

    line = &buf->lines[line_i];
    line->attribs = xreallocarray(line->attribs, line->n,
            sizeof(*line->attribs));
    memset(line->attribs, 0, sizeof(*line->attribs) * line->n);

    ctx.buf = buf;
    ctx.pos.col = 0;
    ctx.pos.line = line_i;
    ctx.state = state;
    ctx.hi = HI_NORMAL;
    ctx.s = line->s;
    ctx.n = line->n;

    clear_parens(buf, line_i);

    for (ctx.pos.col = 0; ctx.pos.col < ctx.n; ) {
        n = (*Langs[buf->lang].fsm[ctx.state & 0xff])(&ctx);
        for (; n > 0; n--) {
            line->attribs[ctx.pos.col] = ctx.hi;
            ctx.pos.col++;
        }
    }

    if (!(ctx.state & (FSTATE_MULTI | FSTATE_FORCE_MULTI))) {
        ctx.state = STATE_START;
    }

    line->state = ctx.state & ~FSTATE_MULTI;
}

bool rehighlight_line(struct buf *buf, line_t line_i)
{
    struct          line *line;
    unsigned        state;
    unsigned        prev_state;
    struct match    *matches;
    size_t          n;

    line = &buf->lines[line_i];
    if (line_i == 0) {
        state = STATE_START;
    } else {
        state = line[-1].state;
    }
    prev_state = line->state;
    highlight_line(buf, line_i, state); 

    if (buf->search_pat != NULL) {
        matches = search_line_pattern(buf, line_i, &n);
        fuse_matches(buf, line_i, matches, n);
        free(matches);
    }

    return line_i + 1 < buf->num_lines && prev_state != line->state;
}

size_t get_next_paren_index(const struct buf *buf, const struct pos *pos)
{
    size_t          l, m, r;
    struct paren    *p;

    l = 0;
    r = buf->num_parens;
    while (l < r) {
        m = (l + r) / 2;

        p = &buf->parens[m];
        if (p->pos.line < pos->line || (p->pos.line == pos->line &&
                                        p->pos.col < pos->col)) {
            l = m + 1;
        } else {
            r = m;
        }
    }
    return l;
}

void add_paren(struct buf *buf, const struct pos *pos, int type)
{
    size_t          index;
    struct paren    *paren;

    if (buf->num_parens + 1 >= buf->a_parens) {
        buf->a_parens *= 2;
        buf->a_parens++;
        buf->parens = xreallocarray(buf->parens, buf->a_parens,
                sizeof(*buf->parens));
    }

    index = get_next_paren_index(buf, pos);
    memmove(&buf->parens[index + 1], &buf->parens[index],
        sizeof(*buf->parens) * (buf->num_parens - index));
    buf->num_parens++;

    paren = &buf->parens[index];
    paren->pos = *pos;
    paren->type = type;
}

size_t get_paren(struct buf *buf, const struct pos *pos)
{
    size_t          first_i, index;

    first_i = get_paren_line(buf, pos->line);
    index = first_i;
    while (index < buf->num_parens &&
            buf->parens[index].pos.line == pos->line) {
        if (buf->parens[index].pos.col > pos->col) {
            break;
        }
        if (buf->parens[index].pos.col == pos->col) {
            return index;
        }
        index++;
    }
    if (index > first_i &&
           buf->parens[index - 1].pos.col + 1 == pos->col) {
       return index - 1;
    }
    /* no parenthesis there */
    return SIZE_MAX;
}

void clear_parens(struct buf *buf, line_t line_i)
{
    size_t          first_i, index;

    index = get_paren_line(buf, line_i);
    first_i = index;
    while (index < buf->num_parens &&
            buf->parens[index].pos.line == line_i) {
        index++;
    }

    memmove(&buf->parens[first_i], &buf->parens[index],
        sizeof(*buf->parens) * (buf->num_parens - index));
    buf->num_parens -= index - first_i;
}

size_t get_matching_paren(struct buf *buf, size_t paren_i)
{
    struct paren    *paren, *p;
    size_t          c;

    paren = &buf->parens[paren_i];
    if ((paren->type & FOPEN_PAREN)) {
        /* set it again because the base pointer might have changed */
        paren = &buf->parens[paren_i];
        /* find the corresponding closing parenthesis */
        for (p = paren, c = 1, p++; p < buf->parens + buf->num_parens; p++) {
            if (((paren->type ^ p->type) & ~FOPEN_PAREN) == 0) {
                if ((p->type & FOPEN_PAREN)) {
                    c++;
                } else {
                    c--;
                    if (c == 0) {
                        return p - buf->parens;
                    }
                }
            }
        }
    } else {
        /* set it again because the base pointer might have changed */
        paren = &buf->parens[paren_i];
        /* find the corresponding opening parenthesis */
        for (p = paren, c = 1; p > buf->parens; ) {
            p--;
            if (((paren->type ^ p->type) & ~FOPEN_PAREN) == 0) {
                if ((p->type & FOPEN_PAREN)) {
                    c--;
                    if (c == 0) {
                        return p - buf->parens;
                    }
                } else {
                    c++;
                }
            }
        }
    }
    /* there was no matching parenthesis */
    return SIZE_MAX;
}
