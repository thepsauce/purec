#include "text.h"
#include "xalloc.h"
#include "purec.h"

#include <magic.h>
#include <iconv.h>
#include <string.h>

void init_text(struct text *text, size_t num_lines)
{
    text->lines = xcalloc(num_lines, sizeof(*text->lines));
    text->num_lines = num_lines;
    text->a_lines = num_lines;
}

void clear_text(struct text *text)
{
    line_t          i;

    for (i = 0; i < text->num_lines; i++) {
        free(text->lines[i].s);
    }
    free(text->lines);
    memset(text, 0, sizeof(*text));
}

void make_text(struct text *text, struct line *lines, line_t num_lines)
{
    text->lines = lines;
    text->num_lines = num_lines;
    text->a_lines = num_lines;
}

void str_to_text(const char *str, size_t len, struct text *text)
{
    const char      *end;

    while (len > 0) {
        for (end = str; len > 0; end++, len--) {
            if (end[0] == '\n') {
                break;
            }
        }
        if (text->num_lines == text->a_lines) {
            text->a_lines *= 2;
            text->a_lines++;
            text->lines = xreallocarray(text->lines, text->a_lines,
                                        sizeof(*text->lines));
        }
        text->lines[text->num_lines].n = end - str;
        text->lines[text->num_lines].s = xmemdup(str, end - str);
        text->num_lines++;
        str = end + 1;
    }
    text->lines = xreallocarray(text->lines, text->num_lines,
                                sizeof(*text->lines));
    text->a_lines = text->num_lines;
}

char *text_to_str(struct text *text, size_t *p_len)
{
    size_t          len;
    line_t          i;
    char            *str, *p;

    len = 0;
    for (i = 0; i < text->num_lines; i++) {
        len += text->lines[i].n + 1;
    }

    str = xmalloc(len);
    p = str;
    for (i = 0; i < text->num_lines; i++) {
        memcpy(p, text->lines[i].s, text->lines[i].n);
        p += text->lines[i].n;
        p[0] = '\n';
        p++;
    }

    *p_len = len;
    return str;
}

size_t read_text(FILE *fp, struct file_rule *rule, struct text *text,
                 line_t max_lines)
{
    static magic_t  magic;
    const char      *from_code;
    iconv_t         icv;
    int             eol;
    char            *ep;
    char            in_buf[1024];
    char            out_buf[1024];
    char            *in_ptr, *out_ptr;
    size_t          in_len, out_len;
    size_t          i, j;
    size_t          num_bytes;
    struct line     *line;

    if (magic == NULL) {
        magic = magic_open(MAGIC_MIME_ENCODING);
        if (magic != NULL) {
            if (magic_load(magic, NULL) == -1) {
                magic_close(magic);
                magic = NULL;
            }
        }
    }

    in_len = fread(in_buf, 1, sizeof(in_buf), fp);
    ep = strchr(in_buf, '\n');
    if (ep != NULL) {
        if (ep > in_buf && ep[-1] == '\r') {
            eol = EOL_CRNL;
        } else {
            eol = EOL_NL;
        }
    } else if (strchr(in_buf, '\r') != NULL) {
        eol = EOL_CR;
    } else {
        eol = EOL_NL;
    }

    from_code = NULL;
    if (magic != NULL) {
        from_code = magic_buffer(magic, in_buf, in_len);
        if (strcmp(from_code, "us-ascii") == 0) {
            from_code = "utf-8";
        }
    }

    if (from_code == NULL) {
        icv = (iconv_t) -1;
        from_code = "utf-8";
    } else {
        icv = iconv_open("utf-8", from_code);
    }

    text->a_lines = 8;
    text->lines = xmalloc(sizeof(*text->lines) * text->a_lines);
    line = &text->lines[0];
    line->s = NULL;
    line->n = 0;
    text->num_lines = 1;
    num_bytes = 0;
    do {
        if (icv == (iconv_t) -1) {
            memcpy(out_buf, in_buf, in_len);
            out_len = in_len;
            in_len = 0;
        } else {
            in_ptr = in_buf;
            out_ptr = out_buf;
            out_len = sizeof(out_buf);
            if (iconv(icv, &in_ptr, &in_len, &out_ptr, &out_len) ==
                    (size_t) -1) {
                out_buf[0] = in_buf[0];
                in_len--;
                in_ptr++;
                out_len = 1;
            } else {
                out_len = sizeof(out_buf) - out_len;
            }
            memmove(in_buf, in_ptr, in_len);
        }
        in_len += fread(&in_buf[in_len], 1, sizeof(in_buf) - in_len, fp);
        i = 0;
        j = 0;
        do {
            for (; i < out_len; i++) {
                if (eol == EOL_CRNL && i + 1 < out_len && out_buf[i] == '\r' &&
                        out_buf[i + 1] == '\n') {
                    break;
                } else if (eol == EOL_CR && out_buf[i] == '\r') {
                    break;
                } else if (eol == EOL_NL && out_buf[i] == '\n') {
                    break;
                }
            }
            line->s = xrealloc(line->s, line->n + i - j);
            memcpy(&line->s[line->n], &out_buf[j], i - j);
            line->n += i - j;
            if (i < out_len) {
                num_bytes += line->n + 1;
                if (text->num_lines == max_lines) {
                    goto end;
                }
                if (text->num_lines == text->a_lines) {
                    text->a_lines *= 2;
                    text->lines = xreallocarray(text->lines, text->a_lines,
                                                sizeof(*text->lines));
                }
                line = &text->lines[text->num_lines++];
                line->s = NULL;
                line->n = 0;
                i += eol == EOL_CRNL ? 2 : 1;
            }
            j = i;
        } while (i < out_len);
    } while (in_len > 0);

    if (icv != (iconv_t) -1) {
        iconv_close(icv);
    }

    num_bytes += line->n;

end:
    if (line->n == 0 && text->num_lines > 1) {
        text->num_lines--;
    }
    if (rule != NULL) {
        rule->encoding = xstrdup(from_code);
        rule->eol = eol;
    }
    return num_bytes;
}

size_t write_text(FILE *fp, const struct file_rule *rule,
                  const struct text *text,
                  line_t from, line_t to)
{
    static const char eols[3][2] = {
        [EOL_NL] = { '\n', '\0' },
        [EOL_CR] = { '\r', '\0' },
        [EOL_CRNL] = { '\r', '\n' },
    };

    iconv_t         icv;
    size_t          num_bytes;
    struct line     *line;
    char            *in_ptr, *out_ptr;
    char            out_buf[1024];
    size_t          out_len, in_len;

    icv = iconv_open(rule->encoding, "utf-8");

    num_bytes = 0;
    for (; from <= to; from++) {
        line = &text->lines[from];
        in_ptr = line->s;
        in_len = line->n;
        while (in_len > 0) {
            out_ptr = out_buf;
            out_len = sizeof(out_buf);
            if (iconv(icv, &in_ptr, &in_len, &out_ptr, &out_len) ==
                    (size_t) -1) {
                out_buf[0] = in_ptr[0];
                out_len = sizeof(out_buf) - 1;
                in_ptr++;
                in_len--;
            }
            fwrite(out_buf, 1, sizeof(out_buf) - out_len, fp);
            num_bytes += sizeof(out_buf) - out_len;
        }
        if (from + 1 != text->num_lines || line->n != 0) {
            fputc(eols[rule->eol][0], fp);
            if (eols[rule->eol][1] != '\0') {
                fputc(eols[rule->eol][1], fp);
            }
            num_bytes++;
        }
    }
    iconv_close(icv);
    return num_bytes;
}

void get_text(const struct text *text,
              const struct pos *from,
              const struct pos *to,
              struct text *dest)
{
    line_t          i;

    if (from->line != to->line) {
        dest->num_lines = to->line - from->line + 1;
        dest->a_lines = dest->num_lines;
        dest->lines = xreallocarray(NULL, dest->num_lines,
                                    sizeof(*dest->lines));
        init_line(&dest->lines[0],
                  &text->lines[from->line].s[from->col],
                  text->lines[from->line].n - from->col);

        for (i = from->line + 1; i < to->line; i++) {
            init_line(&dest->lines[i - from->line],
                      &text->lines[i].s[0], text->lines[i].n);
        }
        if (to->line != text->num_lines) {
            init_line(&dest->lines[dest->num_lines - 1],
                      text->lines[to->line].s, to->col);
        } else {
            init_zero_line(&dest->lines[dest->num_lines - 1]);
        }
    } else {
        dest->num_lines = 1;
        dest->a_lines = 1;
        dest->lines = xmalloc(sizeof(*dest->lines));
        init_line(&dest->lines[0],
                  &text->lines[from->line].s[from->col],
                  to->col - from->col);
    }
}

void get_text_block(const struct text *text,
               const struct pos *from,
               const struct pos *to,
               struct text *dest)
{
    line_t          i;
    struct line     *line;
    col_t           to_col;

    dest->num_lines = to->line - from->line + 1;
    dest->a_lines = dest->num_lines;
    dest->lines = xreallocarray(NULL, dest->num_lines,
                                sizeof(*dest->lines));
    for (i = from->line; i <= to->line; i++) {
        line = &text->lines[i];
        if (from->col >= line->n) {
            init_zero_line(&dest->lines[i - from->line]);
            continue;
        }
        to_col = MIN(to->col + 1, line->n);
        if (to_col == 0) {
            init_zero_line(&dest->lines[i - from->line]);
            continue;
        }
        init_line(&dest->lines[i - from->line],
                  &line->s[from->col],
                  to_col - from->col);
    }
}

bool clip_range(const struct text *text,
                const struct pos *s_from, const struct pos *s_to,
                struct pos *p_from, struct pos *p_to)
{
    struct pos      from, to;

    from = *s_from;
    to = *s_to;

    /* swap if needed */
    sort_positions(&from, &to);

    /* clip lines */
    if (from.line >= text->num_lines) {
        from.line = text->num_lines - 1;
    }
    /* to is allowed to go out of bounds */
    if (to.line > text->num_lines) {
        to.line = text->num_lines;
    }

    /* clip columns */
    from.col = MIN(from.col, text->lines[from.line].n);
    if (to.line == text->num_lines) {
        to.line--;
        to.col = text->lines[to.line].n;
    } else {
        to.col = MIN(to.col, text->lines[to.line].n);
    }

    /* make sure there is text to delete */
    if (from.line == to.line && from.col == to.col) {
        return false;
    }
    *p_from = from;
    *p_to = to;
    return true;
}

bool clip_block(const struct text *text,
                const struct pos *s_from, const struct pos *s_to,
                struct pos *p_from, struct pos *p_to)
{
    struct pos      from, to;

    from = *s_from;
    to = *s_to;
    sort_block_positions(&from, &to);
    if (from.line >= text->num_lines) {
        return false;
    }
    to.line = MIN(to.line, text->num_lines - 1);
    *p_from = from;
    *p_to = to;
    return true;
}

struct line *insert_blank(struct text *text, line_t line, line_t num_lines)
{
    if (text->num_lines + num_lines > text->a_lines) {
        text->a_lines *= 2;
        text->a_lines += num_lines;
        text->lines = xreallocarray(text->lines, text->a_lines,
                                    sizeof(*text->lines));
    }

    /* move tail of the line list to form a gap */
    memmove(&text->lines[line + num_lines],
            &text->lines[line],
            sizeof(*text->lines) * (text->num_lines - line));
    text->num_lines += num_lines;
    /* zero initialize the gap */
    memset(&text->lines[line], 0, sizeof(*text->lines) * num_lines);
    return &text->lines[line];
}

void insert_text(struct text *text,
                 const struct pos *pos,
                 const struct text *src)
{
    const struct line       *s_line;
    struct line             *line, *at_line;
    line_t                  i;
    col_t                   old_n;

    if (src->num_lines == 1) {
        s_line = &src->lines[0];
        line = &text->lines[pos->line];
        line->s = xrealloc(line->s, line->n + s_line->n);
        memmove(&line->s[pos->col + s_line->n], &line->s[pos->col],
                line->n - pos->col);
        memcpy(&line->s[pos->col], s_line->s, s_line->n);
        line->n += s_line->n;
        return;
    }

    line = insert_blank(text, pos->line + 1, src->num_lines - 1);
    for (i = 1; i < src->num_lines; i++) {
        s_line = &src->lines[i];
        line->n = s_line->n;
        line->s = xmalloc(line->n);
        memcpy(line->s, s_line->s, s_line->n);
        line++;
    }

    at_line = &text->lines[pos->line];

    /* add the end of the first line to the end of the last line */
    line--; /* `line` overshoots the last line, so decrement it */
    old_n = line->n;
    line->n += at_line->n - pos->col;
    line->s = xrealloc(line->s, line->n);
    memcpy(&line->s[old_n], &at_line->s[pos->col], at_line->n - pos->col);

    /* trim first line and insert first text segment */
    at_line->n = pos->col + src->lines[0].n;
    at_line->s = xrealloc(at_line->s, at_line->n);
    memcpy(&at_line->s[pos->col], src->lines[0].s, src->lines[0].n);
}

void insert_text_block(struct text *text,
                  const struct pos *pos,
                  const struct text *src)
{
    line_t                  i;
    const struct line       *s_line;
    struct line             *line;
    line_t                  col;

    for (i = 0; i < src->num_lines; i++) {
        s_line = &src->lines[i];
        line = &text->lines[pos->line + i];
        col = pos->col;
        if (s_line->n == 0 || col > line->n) {
            continue;
        }

        line->s = xrealloc(line->s, line->n + s_line->n);
        memmove(&line->s[col + s_line->n], &line->s[col], line->n - col);
        memcpy(&line->s[col], s_line->s, s_line->n);
        line->n += s_line->n;
    }
}

void delete_text(struct text *text,
                 const struct pos *from,
                 const struct pos *to)
{
    line_t          i;
    struct line     *first, *last;

    first = &text->lines[from->line];
    if (from->line == to->line) {
        first->n -= to->col;
        memmove(&first->s[from->col],
                &first->s[to->col], first->n);
        first->n += from->col;
        first->s = xrealloc(first->s, first->n);
    } else if (to->line >= text->num_lines) {
        /* trim the line */
        first->n = from->col;
        first->s = xrealloc(first->s, first->n);
        /* delete the remaining lines */
        for (i = from->line + 1; i < text->num_lines; i++) {
            free(text->lines[i].s);
        }
        text->num_lines = from->line + 1;
    } else {
        last = &text->lines[to->line];
        /* join the current line with the last line */
        first->n = from->col + last->n - to->col;
        first->s = xrealloc(first->s, first->n);
        memcpy(&first->s[from->col],
               &last->s[to->col], last->n - to->col);
        /* delete the remaining lines */
        for (i = from->line + 1; i <= to->line; i++) {
            free(text->lines[i].s);
        }
        text->num_lines -= to->line;
        memmove(&text->lines[from->line + 1],
                &text->lines[to->line + 1],
                sizeof(*text->lines) * (text->num_lines - 1));
        text->num_lines += from->line;
    }
}

void delete_text_block(struct text *text,
                  const struct pos *from,
                  const struct pos *to)
{
    line_t          i;
    struct line     *line;
    col_t           to_col;

    for (i = from->line; i <= to->line; i++) {
        line = &text->lines[i];
        if (from->col >= line->n) {
            continue;
        }
        to_col = MIN(to->col + 1, line->n);
        if (to_col == 0) {
            continue;
        }
        line->n -= to_col;
        memmove(&line->s[from->col],
                &line->s[to_col], line->n);
        line->n += from->col;
    }
}

void repeat_text(struct text *text, const struct text *src, size_t count)
{
    struct line     *line;
    line_t          r, i;

    if (src->num_lines == 1) {
        text->num_lines = 1;
        text->a_lines = text->num_lines;
        text->lines = xmalloc(sizeof(*text->lines) * text->num_lines);
        line = &text->lines[0];
        line->n = src->lines[0].n * count;
        line->s = xmalloc(src->lines[0].n * count);
        for (r = 0; r + src->lines[0].n <= line->n; ) {
            memcpy(&line->s[r], src->lines[0].s, src->lines[0].n);
            r += src->lines[0].n;
        }
        memset(&line->s[r], ' ', line->n - r);
    } else {
        text->num_lines = src->num_lines * count;
        text->a_lines = text->num_lines;
        text->lines = xmalloc(sizeof(*text->lines) * text->num_lines);
        line = &text->lines[0];
        for (r = 0; r + src->num_lines <= text->num_lines;
             r += src->num_lines) {
            for (i = 0; i < src->num_lines; i++) {
                init_line(line, src->lines[i].s, src->lines[i].n);
                line++;
            }
        }
        for (; r < text->num_lines; r++) {
            init_zero_line(line);
            line++;
        }
    }
}

void repeat_text_block(struct text *text, const struct text *src, size_t count)
{
    line_t          i;
    struct line     *line;
    struct line     *s_line;
    col_t           j;

    text->num_lines = src->num_lines;
    text->a_lines = text->num_lines;
    text->lines = xmalloc(sizeof(*text->lines) * text->num_lines);
    for (i = 0; i < text->num_lines; i++) {
        line = &text->lines[i];
        s_line = &src->lines[i];
        line->n = s_line->n * count;
        line->s = xmalloc(line->n);
        if (line->n == 0) {
            continue;
        }
        for (j = 0; j + s_line->n <= line->n; ) {
            memcpy(&line->s[j], s_line->s, s_line->n);
            j += s_line->n;
        }
        memset(&line->s[j], ' ', line->n - j);
    }
}
