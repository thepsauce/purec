#include "parse.h"
#include "xalloc.h"

#include <stdlib.h>

void copy_group(struct group *dest, const struct group *src)
{
    size_t          i;

    dest->parent = NULL;
    dest->type = src->type;
    dest->v = src->v;
    dest->children = NULL;
    dest->num_children = 0;
    switch (src->type) {
    case GROUP_NUMBER:
        dest->v.f = src->v.f;
        break;

    case GROUP_VARIABLE:
        dest->v.w = src->v.w;
        break;

    case GROUP_STRING:
        dest->v.s.p = xmemdup(src->v.s.p, src->v.s.n);
        dest->v.s.n = src->v.s.n;
        break;

    default:
        dest->children = xcalloc(src->num_children, sizeof(*dest->children));
        dest->num_children = src->num_children;
        for (i = 0; i < dest->num_children; i++) {
            copy_group(&dest->children[i], &src->children[i]);
            dest->children[i].parent = dest;
        }
    }
}

struct group *surround_group(struct group *group, int type, size_t n)
{
    struct group    *c;
    size_t          i;

    c = xcalloc(n, sizeof(*c));
    c[0] = *group;
    /* reparent because the pointer changes */
    for (i = 0; i < group->num_children; i++) {
        group->children[i].parent = c;
    }
    group->type = type;
    group->children = c;
    group->num_children = n;
    /* initialize parent */
    for (i = 0; i < n; i++) {
        c[i].parent = group;
    }
    return &c[n - 1];
}

void clear_group(struct group *g)
{
    size_t          i;

    if (g->type == GROUP_STRING) {
        free(g->v.s.p);
    }
    for (i = 0; i < g->num_children; i++) {
        clear_group(&g->children[i]);
    }
    free(g->children);
}

void free_group(struct group *g)
{
    clear_group(g);
    free(g);
}
