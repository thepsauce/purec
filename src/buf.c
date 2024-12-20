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

size_t detect_language(struct buf *buf)
{
    static const char       *commit_detect =
        "# Please enter the commit message";
    line_t                  i;
    col_t                   j;
    int                     l;
    char                    *name;
    struct regex_group      *group;
    struct regex_matcher    matcher;

    if (buf->text.num_lines > 4) {
        if (buf->text.lines[0].n == 0) {
            if (buf->text.lines[1].n > (col_t) strlen(commit_detect) &&
                    memcmp(buf->text.lines[1].s, commit_detect,
                           strlen(commit_detect)) == 0) {
                return COMMIT_LANG;
            }
        }
    }
    for (i = 0; i < buf->text.num_lines; i++) {
        for (j = 0; j < buf->text.lines[i].n; j++) {
            if (isspace(buf->text.lines[i].s[j])) {
                continue;
            }

            if (buf->text.lines[i].s[j] == '#') {
                if (j + 1 == buf->text.lines[i].n ||
                        buf->text.lines[i].s[j + 1] != ' ') {
                    return C_LANG;
                }
            }
            i = buf->text.num_lines - 1;
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
    memset(&matcher, 0, sizeof(matcher));
    matcher.num_lines = 1;
    matcher.lines = xmalloc(sizeof(*matcher.lines));
    matcher.lines[0].s = name;
    matcher.lines[0].n = strlen(name);
    for (l = 1; l < NUM_LANGS; l++) {
        group = parse_regex(Langs[l].file_exts);
        matcher.pos.col = 0;
        matcher.pos.line = 0;
        if (match_regex(group, &matcher) == 0) {
            return l;
        }
        free_regex_group(group);
    }
    free(matcher.lines);
    return NO_LANG;
}

static void analyze_indent_rules(struct buf *buf)
{
    line_t          num;
    struct line     *line;

    num = MIN(buf->text.num_lines, 300);
    for (line = buf->text.lines;
         line < &buf->text.lines[num];
         line++) {
        if (line->n == 0) {
            continue;
        }
        if (line->s[0] == '\t') {
            buf->rule.use_spaces = false;
            break;
        }
        if (line->n == 1) {
            continue;
        }
        if (line->s[0] == ' ' && line->s[1] == ' ') {
            buf->rule.use_spaces = true;
            buf->rule.tab_size = get_line_indent(buf, line - buf->text.lines,
                                                 NULL);
            break;
        }
    }
}

int init_load_buffer(struct buf *buf)
{
    FILE            *fp;
    char            *file;

    buf->rule = Core.rule;

beg:
    if (buf->path == NULL || (fp = fopen(buf->path, "r")) == NULL) {
        buf->file.encoding = xstrdup("utf-8");
        buf->file.eol = EOL_NL;
        init_text(&buf->text, 1);
        notice_line_growth(buf, 0, 1);
        /* this will detect the language based on the file extension */
        buf->lang = detect_language(buf);
        return 1;
    }

    if (stat(buf->path, &buf->st) == 0 && (buf->st.st_mode & S_IFDIR)) {
        file = choose_file(buf->path);
        if (file != NULL) {
            free(buf->path);
            buf->path = get_absolute_path(file);
            free(file);
        }
        goto beg;
    }

    if (read_text(fp, &buf->file, &buf->text, LINE_MAX) == 0) {
        buf->file.encoding = xstrdup("utf-8");
        buf->file.eol = EOL_NL;
        init_text(&buf->text, 1);
        notice_line_growth(buf, 0, 1);
    } else {
        notice_line_growth(buf, 0, buf->text.num_lines);
        analyze_indent_rules(buf);
    }

    set_language(buf, detect_language(buf));
    return 0;
}

void destroy_buffer(struct buf *buf)
{
    struct buf      *prev;
    line_t          i;

    free(buf->path);
    for (i = 0; i < buf->text.num_lines; i++) {
        free(buf->attribs[i]);
    }
    free(buf->attribs);
    free(buf->states);
    clear_text(&buf->text);
    free(buf->file.encoding);
    free(buf->events);
    free(buf->parens);
    free(buf->matches);
    free(buf->search_pat);
    free_regex_group(buf->search_group);

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
    struct buf      *buf;
    size_t          c = 0;

    for (buf = FirstBuffer; buf != NULL; buf = buf->next) {
        c++;
    }
    return c;
}

void set_language(struct buf *buf, size_t lang)
{
    buf->lang = lang;
    rehighlight_lines(buf, 0, buf->text.num_lines);
}

size_t write_file(struct buf *buf, line_t from, line_t to, FILE *fp)
{
    /* clip arguments */
    to = MIN(to, buf->text.num_lines - 1);
    if (from > to) {
        return 0;
    }
    from = MIN(from, buf->text.num_lines - 1);

    return write_text(fp, &buf->file, &buf->text, from, to);
}

struct undo_event *read_file(struct buf *buf, const struct pos *pos, FILE *fp)
{
    struct text         text;
    struct undo_event   *ev;

    if (read_text(fp, NULL, &text, LINE_MAX) == 0) {
        return NULL;
    }
    ev = insert_lines(buf, pos, &text, 1);
    clear_text(&text);
    return ev;
}

col_t get_line_indent(struct buf *buf, line_t line_i, col_t *p_col)
{
    col_t           indent;
    col_t           col;
    struct          line *line;

    line = &buf->text.lines[line_i];
    indent = 0;
    col = 0;
    for (col = 0; col < line->n; col++) {
        if (line->s[col] == ' ') {
            indent++;
        } else if (line->s[col] == '\t') {
            indent += buf->rule.tab_size;
        } else {
            break;
        }
    }
    if (p_col != NULL) {
        *p_col = col;
    }
    return indent;
}

struct undo_event *set_line_indent(struct buf *buf, line_t line_i, col_t indent)
{
    col_t           d;
    struct pos      pos, to;
    struct text     text;

    init_text(&text, 1);
    if (buf->rule.use_spaces) {
        text.lines[0].n = indent;
        text.lines[0].s = xmalloc(indent);
        memset(text.lines[0].s, ' ', indent);
    } else {
        d = indent / buf->rule.tab_size;
        text.lines[0].n = d + indent % buf->rule.tab_size;
        text.lines[0].s = xmalloc(text.lines[0].n);
        memset(text.lines[0].s, '\t', d);
        memset(&text.lines[0].s[d], ' ', text.lines[0].n - d);
    }
    pos.col = 0;
    pos.line = line_i;
    (void) get_line_indent(buf, line_i, &to.col);
    to.line = line_i;
    return _replace_lines(buf, &pos, &to, &text);
}

struct undo_event *indent_line(struct buf *buf, line_t line_i)
{
    col_t           new_indent;

    new_indent = Langs[buf->lang].indentor(buf, line_i);
    return set_line_indent(buf, line_i, new_indent);
}

void insert_lines_no_event(struct buf *buf, const struct pos *pos,
                           const struct text *text)
{
    insert_text(&buf->text, pos, text);
    if (text->num_lines > 1) {
        notice_line_growth(buf, pos->line + 1, text->num_lines - 1);
    }
    rehighlight_lines(buf, pos->line, text->num_lines);
}

struct undo_event *insert_lines(struct buf *buf, const struct pos *pos,
                                const struct text *text, size_t repeat)
{
    struct text     ins;

    repeat_text(&ins, text, repeat);
    return _insert_lines(buf, pos, &ins);
}

struct undo_event *_insert_lines(struct buf *buf, const struct pos *pos,
                                 struct text *text)
{
    insert_lines_no_event(buf, pos, text);
    return add_event(buf, IS_INSERTION, pos, text);
}

void insert_block_no_event(struct buf *buf, const struct pos *pos,
                           const struct text *text)
{
    insert_text_block(&buf->text, pos, text);
    rehighlight_lines(buf, pos->line, text->num_lines);
}

struct undo_event *insert_block(struct buf *buf, const struct pos *pos,
                                const struct text *text, size_t repeat)
{
    struct text     ins;

    repeat_text_block(&ins, text, repeat);
    return _insert_block(buf, pos, &ins);
}

struct undo_event *_insert_block(struct buf *buf,
                   const struct pos *pos,
                   struct text *text)
{
    insert_block_no_event(buf, pos, text);
    return add_event(buf, IS_BLOCK | IS_INSERTION, pos, text);
}

struct undo_event *break_line(struct buf *buf, const struct pos *pos)
{
    col_t               indent;
    struct text         text;
    col_t               d;
    struct pos          p;
    struct undo_event   *ev;

    p = *pos;

    init_text(&text, 2);
    ev = _insert_lines(buf, &p, &text);

    indent = Langs[buf->lang].indentor(buf, pos->line + 1);
    if (indent == 0) {
        return ev;
    }
    p.col = 0;
    p.line++;
    init_text(&text, 1);
    if (buf->rule.use_spaces) {
        text.lines[0].n = indent;
        text.lines[0].s = xmalloc(indent);
        memset(text.lines[0].s, ' ', indent);
    } else {
        d = indent / buf->rule.tab_size;
        text.lines[0].n = d + indent % buf->rule.tab_size;
        text.lines[0].s = xmalloc(text.lines[0].n);
        memset(text.lines[0].s, '\t', d);
        memset(&text.lines[0].s[d], ' ', text.lines[0].n - d);
    }
    ev = _insert_lines(buf, &p, &text);
    ev->flags |= IS_AUTO_INDENT;
    return ev;
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

void notice_line_growth(struct buf *buf, line_t line_i, line_t num_lines)
{
    size_t          index;

    buf->states = xrealloc(buf->states, sizeof(*buf->states) *
                                buf->text.num_lines);
    memmove(&buf->states[line_i + num_lines],
            &buf->states[line_i],
            sizeof(*buf->states) * (buf->text.num_lines - line_i - num_lines));
    memset(&buf->states[line_i], 0, sizeof(*buf->states) * num_lines);

    buf->attribs = xrealloc(buf->attribs, sizeof(*buf->attribs) *
                                buf->text.num_lines);
    memmove(&buf->attribs[line_i + num_lines],
            &buf->attribs[line_i],
            sizeof(*buf->attribs) * (buf->text.num_lines - line_i - num_lines));
    memset(&buf->attribs[line_i], 0, sizeof(*buf->attribs) * num_lines);

    index = get_paren_line(buf, line_i);
    for (; index < buf->num_parens; index++) {
        buf->parens[index].pos.line += num_lines;
    }

    index = get_match_line(buf, line_i);
    for (; index < buf->num_matches; index++) {
        buf->matches[index].from.line += num_lines;
        buf->matches[index].to  .line += num_lines;
    }
}

void notice_line_removal(struct buf *buf, line_t line_i, line_t num_lines)
{
    line_t          i;
    size_t          index;
    size_t          end;

    memmove(&buf->states[line_i],
            &buf->states[line_i + num_lines],
            sizeof(*buf->states) * (buf->text.num_lines - line_i));

    for (i = 0; i < num_lines; i++) {
        free(buf->attribs[line_i + i]);
    }
    memmove(&buf->attribs[line_i],
            &buf->attribs[line_i + num_lines],
            sizeof(*buf->attribs) * (buf->text.num_lines - line_i));

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

void delete_range_no_event(struct buf *buf,
                           const struct pos *from,
                           const struct pos *to)
{
    delete_text(&buf->text, from, to);
    if (from->line != to->line) {
        notice_line_removal(buf, from->line + 1, to->line - from->line);
    }
    rehighlight_lines(buf, from->line, 2);
}

struct undo_event *delete_range(struct buf *buf,
                                const struct pos *from,
                                const struct pos *to)
{
    struct pos          r_from, r_to;
    struct text         text;

    if (!clip_range(&buf->text, from, to, &r_from, &r_to)) {
        return NULL;
    }
    /* get the deleted text for undo */
    get_text(&buf->text, &r_from, &r_to, &text);
    delete_range_no_event(buf, &r_from, &r_to);
    return add_event(buf, IS_DELETION, &r_from, &text);;
}

void delete_block_no_event(struct buf *buf,
                           const struct pos *from,
                           const struct pos *to)
{
    delete_text_block(&buf->text, from, to);
    rehighlight_lines(buf, from->line, to->line - from->line + 1);
}

struct undo_event *delete_block(struct buf *buf,
                                const struct pos *from,
                                const struct pos *to)
{
    struct pos          r_from, r_to;
    struct text         text;

    if (!clip_block(&buf->text, from, to, &r_from, &r_to)) {
        return NULL;
    }
    /* get the deleted text for undo */
    get_text_block(&buf->text, &r_from, &r_to, &text);
    delete_block_no_event(buf, &r_from, &r_to);
    return add_event(buf, IS_BLOCK | IS_DELETION, &r_from, &text);
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
    struct text         *text;
    struct line         *line;
    col_t               to_col;
    struct undo_event   *ev;
    struct pos          pos;
    struct text         chg;
    char                ch;
    line_t              i;
    col_t               j;

    from = *pfrom;
    to = *pto;
    text = &buf->text;

    sort_block_positions(&from, &to);

    if (from.line >= text->num_lines) {
        return NULL;
    }

    to.line = MIN(to.line, text->num_lines - 1);

    ev = NULL;
    for (i = from.line; i <= to.line; i++) {
        line = &text->lines[i];
        if (from.col >= line->n) {
            continue;
        }
        to_col = MIN(to.col + 1, line->n);
        if (to_col == 0) {
            continue;
        }

        pos.line = line - text->lines;
        pos.col = from.col;
        chg.lines = xmalloc(sizeof(*chg.lines));
        init_line(&chg.lines[0], &line->s[from.col], to_col - from.col);
        for (j = from.col; j < to_col; j++) {
            ch = (*conv)(line->s[j]);
            chg.lines[0].s[j - from.col] ^= ch;
            line->s[j] = ch;
        }
        chg.num_lines = 1;
        ev = add_event(buf, IS_REPLACE, &pos, &chg);
    }
    rehighlight_lines(buf, from.line, to.line - from.line + 1);
    return ev;
}

struct undo_event *change_range(struct buf *buf, const struct pos *pfrom,
        const struct pos *pto, int (*conv)(int))
{
    struct pos      from, to;
    struct text     *text;
    struct line     *lines;
    line_t          num_lines;
    struct line     *line;
    char            ch;
    line_t          i;
    col_t           j;
    struct text     chg;

    from = *pfrom;
    to = *pto;
    text = &buf->text;

    /* clip lines */
    if (from.line >= text->num_lines) {
        from.line = text->num_lines - 1;
    }
    /* to is allowed to go out of bounds here */
    if (to.line > text->num_lines) {
        to.line = text->num_lines;
    }

    /* swap if needed */
    sort_positions(&from, &to);

    /* clip columns */
    from.col = MIN(from.col, text->lines[from.line].n);
    if (to.line == text->num_lines) {
        to.line--;
        to.col = text->lines[to.line].n;
    } else {
        to.col = MIN(to.col, text->lines[to.line].n);
    }

    /* make sure there is text to change */

    /* there can be nothing to replace when the cursor is at the end of a
     * line, so this moves `from` as high up as possible where as the latter
     * moves `to` as much down as possible
     */
    while (from.line < to.line && from.col == text->lines[from.line].n) {
        from.line++;
        from.col = 0;
    }
    while (to.col == 0 && to.line > from.line) {
        to.line--;
        to.col = text->lines[to.line].n;
    }

    if (from.line == to.line && from.col == to.col) {
        return 0;
    }

    if (from.line != to.line) {
        num_lines = to.line - from.line + 1;
        lines = xreallocarray(NULL, num_lines, sizeof(*lines));

        line = &text->lines[from.line];
        init_line(&lines[0], &line->s[from.col], line->n - from.col);
        for (i = from.col; i < line->n; i++) {
            ch = (*conv)(line->s[i]);
            lines[0].s[i - from.col] ^= ch;
            line->s[i] = ch;
        }

        for (i = from.line + 1; i < to.line; i++) {
            line = &text->lines[i];
            init_line(&lines[i - from.line], &line->s[0], line->n);
            for (j = 0; j < line->n; j++) {
                ch = (*conv)(line->s[j]);
                lines[i - from.line].s[j] ^= ch;
                line->s[j] = ch;
            }
        }

        line = &text->lines[to.line];
        init_line(&lines[to.line - from.line], &line->s[0], to.col);
        for (i = 0; i < to.col; i++) {
            ch = (*conv)(line->s[i]);
            lines[to.line - from.line].s[i] ^= ch;
            line->s[i] = ch;
        }
    } else {
        num_lines = 1;
        lines = xmalloc(sizeof(*lines));
        line = &text->lines[from.line];
        init_line(&lines[0], &line->s[from.col], to.col - from.col);
        for (i = from.col; i < to.col; i++) {
            ch = (*conv)(line->s[i]);
            lines[0].s[i - from.col] ^= ch;
            line->s[i] = ch;
        }
    }

    rehighlight_lines(buf, from.line, to.line - from.line + 1);

    make_text(&chg, lines, num_lines);
    return add_event(buf, IS_REPLACE, &from, &chg);
}

struct undo_event *replace_lines(struct buf *buf,
                                 const struct pos *from,
                                 const struct pos *to,
                                 const struct text *text)
{
    struct undo_event   *ev;
    struct undo_event   *ev2;

    /* TODO: optimize this */
    ev = delete_range(buf, from, to);
    ev2 = insert_lines(buf, from, text, 1);

    return ev == NULL ? ev2 : ev2 == NULL ? ev : ev2 - 1;
}

struct undo_event *_replace_lines(struct buf *buf,
                                 const struct pos *from,
                                 const struct pos *to,
                                 struct text *text)
{
    struct undo_event   *ev;

    /* TODO: optimize this */
    ev = delete_range(buf, from, to);
    if (text->num_lines == 1 && text->lines[0].n == 0) {
        free(text->lines);
        return ev;
    }
    (void) _insert_lines(buf, from, text);
    return ev;
}

static struct match *search_pattern(struct buf *buf, struct pos *from,
                                    struct pos *to, size_t *p_num_matches)
{
    struct match            *matches, match;
    size_t                  a_matches, num_matches;
    struct regex_matcher    matcher;
    struct pos              p;

    a_matches = 8;
    matches = xmalloc(sizeof(*matches) * a_matches);
    num_matches = 0;
    matcher.lines = buf->text.lines;
    matcher.num_lines = buf->text.num_lines;

    p = *from;
    while (1) {
        if ((p.line == to->line && p.col > to->col) || p.line > to->line) {
            break;
        }
        matcher.pos = p;
        matcher.num = 0;
        match.from = matcher.pos;
        if (match_regex(buf->search_group, &matcher) == 1) {
            if (p.col == buf->text.lines[p.line].n) {
                p.col = 0;
                p.line++;
            } else {
                p.col++;
            }
        } else {
            match.to = matcher.pos;
            match.num = matcher.num;
            memcpy(match.sub, matcher.sub, match.num * sizeof(*match.sub));
            if (num_matches == a_matches) {
                a_matches *= 2;
                matches = xreallocarray(matches, a_matches, sizeof(*matches));
            }
            matches[num_matches++] = match;
            if (is_point_equal(&p, &matcher.pos)) {
                if (p.col == buf->text.lines[p.line].n) {
                    p.col = 0;
                    p.line++;
                } else {
                    p.col++;
                }
            } else {
                p = matcher.pos;
            }
        }
    }
    *p_num_matches = num_matches;
    return matches;
}

size_t set_pattern(struct buf *buf, const char *pat)
{
    struct pos      from, to;
    size_t          n;
    struct match    *matches;

    free(buf->search_pat);
    buf->search_pat = xstrdup(pat);
    free_regex_group(buf->search_group);
    buf->search_group = parse_regex(pat);

    free(buf->matches);

    from.col = 0;
    from.line = 0;
    to.col = buf->text.lines[buf->text.num_lines - 1].n;
    to.line = buf->text.num_lines - 1;
    matches = search_pattern(buf, &from, &to, &n);
    matches = xreallocarray(matches, n, sizeof(*matches));
    buf->matches = matches;
    buf->num_matches = n;
    buf->a_matches = n;
    return n;
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
    int                 *attribs;

    clear_parens(buf, line_i);

    line = &buf->text.lines[line_i];

    attribs = buf->attribs[line_i];
    attribs = xreallocarray(attribs, line->n, sizeof(*attribs));
    buf->attribs[line_i] = attribs;

    memset(attribs, 0, sizeof(*attribs) * line->n);

    ctx.buf = buf;
    ctx.pos.col = 0;
    ctx.pos.line = line_i;
    ctx.state = state;
    ctx.hi = HI_NORMAL;
    ctx.s = line->s;
    ctx.n = line->n;

    for (ctx.pos.col = 0; ctx.pos.col < ctx.n; ) {
        n = (*Langs[buf->lang].fsm[ctx.state & 0xff])(&ctx);
        for (; n > 0; n--) {
            attribs[ctx.pos.col] = ctx.hi;
            ctx.pos.col++;
        }
    }

    if (!(ctx.state & (FSTATE_MULTI | FSTATE_FORCE_MULTI))) {
        ctx.state = STATE_START;
    }

    buf->states[line_i] = ctx.state & ~FSTATE_MULTI;
}

static void fuse_matches(struct buf *buf, size_t start, size_t end,
                         struct match *matches, size_t num_matches)
{
    size_t          take;

    take = end - start;
    if (buf->num_matches + num_matches - take > buf->a_matches) {
        buf->a_matches *= 2;
        buf->a_matches += num_matches - take;
        buf->matches = xreallocarray(buf->matches, buf->a_matches,
                                     sizeof(*buf->matches));
    }
    memmove(&buf->matches[start + num_matches],
            &buf->matches[end],
            sizeof(*buf->matches) * (buf->num_matches - end));
    memcpy(&buf->matches[start], matches, sizeof(*matches) * num_matches);
    buf->num_matches += num_matches - take;
}

void rehighlight_lines(struct buf *buf, line_t line_i, line_t num_lines)
{
    struct pos          from, to;
    struct match        *matches;
    size_t              num_matches;
    unsigned            state, prev_state;

    if (buf->search_pat != NULL) {
        /* TODO: check if multi line match or not */
        from.col = 0;
        from.line = 0;
        to.line = buf->text.num_lines - 1;
        to.col = buf->text.lines[to.line].n;
        matches = search_pattern(buf, &from, &to, &num_matches);
        fuse_matches(buf, 0, buf->num_matches, matches, num_matches);
        free(matches);
    }

    state = line_i == 0 ? STATE_START : buf->states[line_i - 1];
    for (; num_lines > 0; num_lines--, line_i++) {
        if (line_i >= buf->text.num_lines) {
            break;
        }
        prev_state = buf->states[line_i];
        highlight_line(buf, line_i, state); 

        if (prev_state != buf->states[line_i]) {
            if (num_lines == 1) {
                num_lines++;
            }
        }
        state = buf->states[line_i];
    }
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
