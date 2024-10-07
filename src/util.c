#include "util.h"
#include "xalloc.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

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
    char *cwd;
    char *s;
    char *p;
    size_t index;
    size_t num_slashes;
    const char *seg;
    size_t move;

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
    size_t      cap;
    char        *err;
    char        *s;
    size_t      n;

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
            err = getcwd(s, cap);
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

int wcwidth(wchar_t c);

int get_glyph(const char *s, size_t n, struct glyph *g)
{
    size_t          c;
    wchar_t         wc;
    mbstate_t       mbs;

    if (s[0] == '\0') {
        g->wc = L'\0';
        g->n = 1;
        g->w = 1;
        return 1;
    }

    memset(&mbs, 0, sizeof(mbs));
    c = mbrtowc(&wc, &s[0], n, &mbs);
    if (c == (size_t) -1 || c == (size_t) -2) {
        g->wc = s[0];
        g->n = 1;
        g->w = 1;
        return -1;
    }

    g->wc = wc;
    g->n = c;
    g->w = wcwidth(wc);
    return c;
}
