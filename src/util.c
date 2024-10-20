#include "util.h"
#include "purec.h"
#include "xalloc.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

int get_glyph_count(const char *s, size_t n)
{
    if (n >= 4 && (s[0] & 0xf8) == 0xf0) {
        if ((s[1] & 0xc0) != 0x80 ||
                (s[2] & 0xc0) != 0x80 ||
                (s[3] & 0xc0) != 0x80) {
            return 1;
        }
        return 4;
    }

    if (n >= 3 && (s[0] & 0xf0) == 0xe0) {
        if ((s[1] & 0xc0) != 0x80 ||
                (s[2] & 0xc0) != 0x80) {
            return 1;
        }
        return 3;
    }

    if (n >= 2 && (s[0] & 0xe0) == 0xc0) {
        if ((s[1] & 0xc0) != 0x80) {
            return 1;
        }
        return 2;
    }
    return 1;
}

size_t move_back_glyph(const char *s, size_t i)
{
    size_t          n;

    if (i == 0) {
        return 0;
    }

    for (n = 1; (s[i - n] & 0xc0) == 0x80; n++) {
        if (n == 4 || i - n == 0) {
            /* this happens when there is an invalid utf8 sequence */
            return i - 1;
        }
    }
    return i - n;
}

size_t get_index(const char *s, size_t n, size_t target_x)
{
    struct glyph    g;
    size_t          i, x;

    for (i = 0, x = 0; i < n && x < target_x; i += g.n, x += g.w) {
        if (s[i] == '\t') {
            g.n = 1;
            g.w = Core.tab_size - x % Core.tab_size;
        } else {
            (void) get_glyph(&s[i], n - i, &g);
        }
    }
    return i;
}

size_t get_advance(const char *s, size_t n, size_t i)
{
    size_t          j, x;
    struct glyph    g;

    i = MIN(i, n);
    for (j = 0, x = 0; j < i; ) {
        if (s[j] == '\t') {
            g.n = 1;
            g.w = Core.tab_size - x % Core.tab_size;
        } else {
            (void) get_glyph(&s[j], n - j, &g);
        }
        j += g.n;
        x += g.w;
    }
    return x;
}

int wcwidth(wchar_t c);

int get_glyph(const char *s, size_t n, struct glyph *g)
{
    wchar_t         wc;

    if ((s[0] >= '\0' && s[0] < ' ') || s[0] == 0x7f) {
        g->wc = s[0];
        g->n = 1;
        g->w = 2;
        return 1;
    }

    if ((s[0] & 0x80) == 0) {
        g->wc = s[0];
        g->n = 1;
        g->w = 1;
        return 1;
    }

    if (n >= 4 && (s[0] & 0xf8) == 0xf0) {
        if ((s[1] & 0xc0) != 0x80 ||
                (s[2] & 0xc0) != 0x80 ||
                (s[3] & 0xc0) != 0x80) {
            goto err;
        }
        wc = (s[0] & 0x07) << 18;
        wc |= (s[1] & 0x3f) << 12;
        wc |= (s[2] & 0x3f) << 6;
        wc |= (s[3] & 0x3f);
        g->n = 4;
    } else if (n >= 3 && (s[0] & 0xf0) == 0xe0) {
        if ((s[1] & 0xc0) != 0x80 ||
                (s[2] & 0xc0) != 0x80) {
            goto err;
        }
        wc = (s[0] & 0x0f) << 12;
        wc |= (s[1] & 0x3f) << 6;
        wc |= (s[2] & 0x3f);
        g->n = 3;
    } else if (n >= 2 && (s[0] & 0xe0) == 0xc0) {
        if ((s[1] & 0xc0) != 0x80) {
            goto err;
        }
        wc = (s[0] & 0x1f) << 6;
        wc |= (s[1] & 0x3f);
        g->n = 2;
    } else {
        goto err;
    }
    g->wc = wc;
    g->w = wcwidth(wc);
    return g->n;

err:
    g->wc = s[0];
    g->n = 1;
    g->w = 1;
    return -1;
}

bool is_point_equal(const struct pos *p1, const struct pos *p2)
{
    return p1->line == p2->line && p1->col == p2->col;
}

void sort_positions(struct pos *p1, struct pos *p2)
{
    struct pos      tmp;

    if (p1->line > p2->line) {
        tmp = *p1;
        *p1 = *p2;
        *p2 = tmp;
    } else if (p1->line == p2->line) {
        if (p1->col > p2->col) {
            tmp.col = p1->col;
            p1->col = p2->col;
            p2->col = tmp.col;
        }
    }
}

void sort_block_positions(struct pos *p1, struct pos *p2)
{
    struct pos      p;

    p = *p1;
    p1->line = MIN(p.line, p2->line);
    p1->col = MIN(p.col, p2->col);
    p2->line = MAX(p.line, p2->line);
    p2->col = MAX(p.col, p2->col);
}

bool is_in_range(const struct pos *pos,
        const struct pos *from, const struct pos *to)
{
    if (pos->line < from->line || pos->line > to->line) {
        return false;
    }
    if (pos->line == from->line && pos->col < from->col) {
        return false;
    }
    if (pos->line == to->line && pos->col > to->col) {
        return false;
    }
    return true;
}

bool is_in_block(const struct pos *pos,
        const struct pos *from, const struct pos *to)
{
    if (pos->line < from->line || pos->line > to->line ||
            pos->col < from->col || pos->col > to->col) {
        return false;
    }
    return true;
}

size_t safe_mul(size_t a, size_t b)
{
    size_t          c;

    if (__builtin_mul_overflow(a, b, &c)) {
        return SIZE_MAX;
    }
    return c;
}

size_t safe_add(size_t a, size_t b)
{
    size_t          c;

    if (__builtin_add_overflow(a, b, &c)) {
        return SIZE_MAX;
    }
    return c;
}

char *get_relative_path(const char *path)
{
    char            *cwd;
    char            *s;
    char            *p;
    size_t          index;
    size_t          num_slashes;
    const char     *seg;
    size_t          move;

    cwd = getcwd(NULL, 0);
    if (cwd == NULL) {
        return NULL;
    }

    /* allocate a good estimate of bytes for later */
    s = xmalloc(strlen(path) + 1);

    p = cwd;
    num_slashes = 0;
    if (path[0] == '/') {
        index = 0;
        while (1) {
            if (p[index] == '\0') {
                if (path[index] == '/') {
                    p += index;
                    path += index + 1;
                }
                break;
            }
            if (path[index] == '\0') {
                if (p[index] == '/') {
                    p += index + 1;
                    path += index;
                }
                break;
            }
            if (p[index] != path[index]) {
                break;
            }
            if (p[index] == '/') {
                index++;
                p += index;
                path += index;
                while (path[0] == '/') {
                    path++;
                }
                index = 0;
            } else {
                index++;
            }
        }

        if (p[0] != '\0') {
            num_slashes++;
        }
        for (; p[0] != '\0'; p++) {
            if (p[0] == '/') {
                num_slashes++;
            }
        }
    }
    /* else the path is already relative to the current path */

    free(cwd);

    index = 0;
    while (1) {
        while (path[0] == '/') {
            path++;
        }
        seg = path;
        while (path[0] != '/' && path[0] != '\0') {
            path++;
        }

        if (seg == path) {
            break;
        }

        if (seg[0] == '.') {
            if (seg[1] == '.') {
                /* move up the path */
                if (seg[2] == '/' || seg[2] == '\0') {
                    if (index == 0) {
                        num_slashes++;
                    } else {
                        /* remove the last directory */
                        for (index--; index > 0 && s[--index] != '/'; ) {
                            (void) 0;
                        }
                    }
                    continue;
                }
            } else if (seg[1] == '/' || seg[1] == '\0') {
                /* simply ignore ./ */
                continue;
            }
        }
        memcpy(&s[index], seg, path - seg);
        index += path - seg;
        s[index++] = '/';
    }

    if (index == 0 && num_slashes == 0) {
        s = xrealloc(s, 2);
        s[0] = '.';
        s[1] = '\0';
        return s;
    }

    move = index;
    index += 3 * num_slashes;
    s = xrealloc(s, index);
    memmove(&s[3 * num_slashes], &s[0], move);
    for (size_t i = 0; i < num_slashes; i++) {
        s[i * 3] = '.';
        s[i * 3 + 1] = '.';
        s[i * 3 + 2] = '/';
    }

    s[index - 1] = '\0';
    return s;
}

char *get_absolute_path(const char *path)
{
    size_t          cap;
    char            *err;
    char            *s;
    size_t          n;

    cap = 512;
    if (path[0] == '/') {
        s = xmalloc(cap);
        n = 0;
        do {
            path++;
        } while (path[0] == '/');
    } else {
        s = NULL;
        do {
            cap *= 2;
            s = xrealloc(s, cap);
            err = getcwd((char*) s, cap);
        } while (err == NULL && errno == ERANGE);

        if (err == NULL) {
            s[0] = '/';
            n = 1;
        } else {
            n = strlen(s);
        }
    }
    
    while (path[0] != '\0') {
        if (path[0] == '.' && (path[1] == '/' || path[1] == '\0')) {
            path++;
        } else if (path[0] == '.' && path[1] == '.' &&
                   (path[2] == '/' || path[2] == '\0')) {
            /* go back a path component */
            path += 2;
            if (n > 0) {
                n--;
                while (s[n] != '/') {
                    n--;
                }
            }
        } else {
            if (n + 1 >= cap) {
                cap *= 2;
                s = xrealloc(s, cap);
            }
            s[n++] = '/';
            while (path[0] != '/' && path[0] != '\0') {
                if (n + 1 >= cap) {
                    cap *= 2;
                    s = xrealloc(s, cap);
                }
                s[n++] = path[0];
                path++;
            }
        }
        while (path[0] == '/') {
            path++;
        }
    }

    if (n == 0) {
        s = xrealloc(s, 2);
        s[0] = '/';
        s[1] = '\0';
        n = 2;
    }

    s = xrealloc(s, n + 1);
    s[n] = '\0';
    return s;
}

ssize_t get_line(char **p_s, size_t *p_a, FILE *fp)
{
    char            *s;
    size_t          a;
    int             c;
    ssize_t         n;

    s = *p_s;
    a = *p_a;
    n = 0;
    while (c = fgetc(fp), c != EOF && c != '\n') {
        if ((size_t) n == a) {
            a *= 2;
            a++;
            s = xrealloc(s, a);
        }
        s[n++] = c;
    }
    if (n == 0 && c == EOF) {
        return -1;
    }
    *p_s = s;
    *p_a = a;
    return n;
}

static bool isidentf(int c)
{
    return isalnum(c) || c == '_';
}

static char get_pat_char(const char *s, const char **e)
{
    char            ch;

    if (s[0] != '\\') {
        *e = s;
        return s[0];
    }
    s++;
    switch (s[0]) {
    case 'a': ch = '\a'; break;
    case 'b': ch = '\b'; break;
    case 'f': ch = '\f'; break;
    case 'n': ch = '\n'; break;
    case 'r': ch = '\r'; break;
    case 't': ch = '\t'; break;
    case 'v': ch = '\v'; break;
    case 'x':
        if (isalpha(s[1])) {
            ch = tolower(s[1]) - 'a';
        } else if (isdigit(s[1])) {
            ch = s[1] - '0';
        } else {
            ch = 'x';
            break;
        }

        ch <<= 4;
        s++;
        if (isalpha(s[1])) {
            ch |= tolower(s[1]) - 'a';
        } else if (isdigit(s[1])) {
            ch |= s[1] - '0';
        } else {
            break;
        }
        s++;
        break;

    default:
        ch = s[0];
    }

    *e = s;
    return ch;
}

bool match_pattern_exact(const char *s, size_t s_len, const char *pat)
{
    size_t          l;

    l = match_pattern(s, 0, s_len, pat);
    if (s[l] == '\0') {
        return true;
    }
    return false;
}

size_t match_pattern(const char *s, size_t i, size_t s_len, const char *pat)
{
    size_t          st_i;
    size_t          j;
    const char      *end;
    char            ch;
    size_t          star;
    bool            ignore_case;

    st_i = i;
    star = SIZE_MAX;
    ignore_case = false;
    for (j = 0; i < s_len; ) {
        switch (pat[j]) {
        case '?':
            j++;
            i++;
            break;

        case '*':
            j++;
            star = j;
            break;

        case '<':
            if (!isidentf(s[i])) {
                goto mismatch;
            }
            if (i > 0 && isidentf(s[i - 1])) {
                goto mismatch;
            }
            j++;
            break;

        case '>':
            if (i > 0 && !isidentf(s[i - 1])) {
                goto mismatch;
            }
            if (isidentf(s[i])) {
                goto mismatch;
            }
            j++;
            break;

        case '^':
            if (i > 0) {
                /* no longer at the beginning */
                return 0;
            }
            j++;
            break;

        case '$':
            /* not at the end yet */
            return 0;

        default:
            if (pat[j] == '\0') {
                goto mismatch;
            }
            if (pat[j] == '\\') {
                switch (pat[j + 1]) {
                case 'c':
                    ignore_case = false;
                    break;

                case 'i':
                    ignore_case = true;
                    break;
                }
                j += 2;
                break;
            }
            ch = get_pat_char(&pat[j], &end);
            if (ignore_case) {
                if (tolower(s[i]) != tolower(ch)) {
                    goto mismatch;
                }
            } else {
                if (s[i] != ch) {
                    goto mismatch;
                }
            }
            i++;
            j += end - &pat[j] + 1;
        }

        /* jump over the `mismatch:` part */
        continue;

    mismatch:
        if (pat[j] == '\0') {
            return i - st_i;
        }
        if (star == SIZE_MAX) {
            return 0;
        }
        /* consume one character */
        i++;
        j = star;
    }

    while (pat[j] == '$') {
        j++;
    }
    if (pat[j] != '\0') {
        return 0;
    }
    return i - st_i;
}
