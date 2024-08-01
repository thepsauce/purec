#include "util.h"

#include <stdint.h>

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

