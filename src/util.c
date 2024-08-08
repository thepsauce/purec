#include "util.h"
#include "xalloc.h"

#include <stdint.h>
#include <string.h>

#include <unistd.h>

bool is_point_equal(const struct pos *p1, const struct pos *p2)
{
    return p1->line == p2->line && p1->col == p2->col;
}

void sort_positions(struct pos *p1, struct pos *p2)
{
    struct pos tmp;

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
    struct pos p;

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
    size_t c;

    if (__builtin_mul_overflow(a, b, &c)) {
        return SIZE_MAX;
    }
    return c;
}

size_t safe_add(size_t a, size_t b)
{
    size_t c;

    if (__builtin_add_overflow(a, b, &c)) {
        return SIZE_MAX;
    }
    return c;
}

char *get_relative_path(const char *path)
{
    char *cwd;
    size_t cwd_len;
    char *s;
    size_t index = 0;
    size_t pref_cwd = 0, pref_path = 0;
    size_t num_slashes = 0;
    const char *seg;
    size_t move;

    cwd = getcwd(NULL, 0);
    if (cwd == NULL) {
        return NULL;
    }
    cwd_len = strlen(cwd);

    if (path[0] == '/') {
        while (cwd[pref_cwd] == path[pref_path]) {
            /* collapse multiple '/////' into a single one */
            if (path[pref_path] == '/') {
                do {
                    pref_path++;
                } while (path[pref_path] == '/');
            } else {
                pref_path++;
            }
            pref_cwd++;
        }

        if (cwd[pref_cwd] != '\0' ||
                (path[pref_path] != '/' &&
                 path[pref_path] != '\0')) {
            /* position right at the previous slash */
            while (pref_cwd > 0 && cwd[pref_cwd] != '/') {
                pref_cwd--;
                pref_path--;
            }
        }
        pref_path++;

        for (size_t i = pref_cwd; i < cwd_len; i++) {
            if (cwd[i] == '/') {
                num_slashes++;
            }
        }
    }
    /* else the path is already relative to the current path */

    /* allocate a good estimate of bytes, it can not be longer than that */
    s = xmalloc(strlen(path) + 1);
    for (path += pref_path;;) {
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
                        for (index--; index > 0 && s[--index] != '/'; ) {
                            (void) 0;
                        }
                    }
                    continue;
                }
            } else if (seg[1] == '/' || seg[1] == '\0') {
                /* simply ignore . */
                continue;
            }
        }
        if (index > 0) {
            s[index++] = '/';
        }
        memcpy(&s[index], seg, path - seg);
        index += path - seg;
    }

    move = index;
    index += 3 * num_slashes;
    /* make sure when only going up that the final '/' is omitted */
    if (index > 0 && index == 3 * num_slashes) {
        index--;
    }
    s = xrealloc(s, index + 1);
    memmove(&s[3 * num_slashes], &s[0], move);
    for (size_t i = 0; i < num_slashes; i++) {
        s[i * 3] = '.';
        s[i * 3 + 1] = '.';
        if (i * 3 + 2 != index) {
            s[i * 3 + 2] = '/';
        }
    }

    if (index == 0) {
        s = xrealloc(s, 2);
        s[index++] = '.';
    }

    s[index] = '\0';

    free(cwd);
    return s;
}
