#include "buf.h"
#include "color.h"
#include "fuzzy.h"
#include "input.h"
#include "purec.h"
#include "util.h"
#include "xalloc.h"

#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <wctype.h>

static int compare_entries(const void *v_a, const void *v_b)
{
    const struct entry *a, *b;

    a = v_a;
    b = v_b;
    if (a->score != b->score) {
        return b->score - a->score;
    }
    if (a->type != b->type) {
        return a->type - b->type;
    }
    return strcasecmp(a->name, b->name);
}

/**
 * Matches the entries in `entries` against the search pattern in the input
 * and sorts the entries such that the matching elements come first.
 */
static void sort_entries(struct fuzzy *fuzzy)
{
    line_t          i;
    size_t          s_i;
    size_t          pat_i;
    int             score;
    int             cons_score;
    struct entry    *entry;
    struct glyph    g_p, g_n;

    if (fuzzy->num_entries == 0) {
        fuzzy->selected = 0;
        return;
    }

    for (i = 0; i < fuzzy->num_entries; i++) {
        entry = &fuzzy->entries[i];
        s_i = 0;
        score = 0;
        cons_score = 1;
        pat_i = fuzzy->inp.prefix;
        while (pat_i < fuzzy->inp.n && entry->name[s_i] != '\0') {
            (void) get_glyph(&fuzzy->inp.s[pat_i], fuzzy->inp.n - pat_i, &g_p);
            (void) get_glyph(&entry->name[s_i], SIZE_MAX, &g_n);
            if (towlower(g_n.wc) == towlower(g_p.wc)) {
                /* additional point if the first matches */
                if (s_i == 0) {
                    score++;
                }
                /* many consecutive matching letters give a bigger score */
                score += cons_score;
                /* additional point if the case matches */
                if (g_n.wc == g_p.wc) {
                    score++;
                }
                cons_score++;
                pat_i += g_p.n;
            } else {
                cons_score = 1;
            }
            s_i += g_n.n;
        }

        if (pat_i == fuzzy->inp.n) {
            entry->score = score;
        } else {
            entry->score = 0;
        }
    }

    qsort(fuzzy->entries, fuzzy->num_entries,
          sizeof(*fuzzy->entries), compare_entries);
    fuzzy->selected = MIN(fuzzy->num_entries - 1, fuzzy->selected);
}

/**
 * Render all entries, this also clears the fuzzy window.
 */
static void render_entries(struct fuzzy *fuzzy)
{
    line_t          i, e;
    struct entry    *entry;
    int             x;
    size_t          s_i;
    size_t          pat_i;
    struct glyph    g_n, g_p;
    bool            broken;

    set_highlight(stdscr, HI_FUZZY);

    e = MIN(fuzzy->num_entries, fuzzy->scroll.line + fuzzy->h);
    for (i = fuzzy->scroll.line; i < e; i++) {
        move(fuzzy->y + 1 + i - fuzzy->scroll.line, fuzzy->x);

        if (fuzzy->selected == i) {
            attr_on(A_REVERSE, NULL);
        } else {
            attr_off(A_REVERSE, NULL);
        }
 
        entry = &fuzzy->entries[i];
        x = 0;
        s_i = 0;
        pat_i = fuzzy->inp.prefix;
        while (entry->name[s_i] != '\0') {
            broken = get_glyph(&entry->name[s_i], SIZE_MAX, &g_n) == -1;
            if (x + g_n.w >= fuzzy->w) {
                break;
            }

            if (entry->score > 0 && pat_i < fuzzy->inp.n) {
                (void) get_glyph(&fuzzy->inp.s[pat_i], fuzzy->inp.n - pat_i, &g_p);
                if (towlower(g_n.wc) == towlower(g_p.wc)) {
                    attr_on(A_BOLD, NULL);
                    pat_i += g_p.n;
                }
            }
            if (broken) {
                addch('?' | A_REVERSE);
            } else {
                addnstr(&entry->name[s_i], g_n.n);
            }
            attr_off(A_BOLD, NULL);
            x += g_n.w;
            s_i += g_n.n;
        }

        if (entry->type == DT_DIR && x < fuzzy->w) {
            addch('/');
            x++;
        }

        /* erase to end of line */
        for (; x < fuzzy->w; x++) {
            addch(' ');
        }
    }

    /* erase the bottom */
    attr_off(A_REVERSE, NULL);
    for (; i < fuzzy->scroll.line + fuzzy->h; i++) {
        move(fuzzy->y + 1 + i - fuzzy->scroll.line, fuzzy->x);
        while (getcurx(stdscr) < fuzzy->x + fuzzy->w) {
            addch(' ');
        }
    }
}

void render_fuzzy(struct fuzzy *fuzzy)
{
    fuzzy->x = COLS / 6;
    fuzzy->y = LINES / 6;
    fuzzy->w = COLS == 1 ? 1 : COLS * 2 / 3;
    fuzzy->h = LINES <= 2 ? LINES : LINES * 2 / 3;

    fuzzy->inp.x = fuzzy->x;
    fuzzy->inp.y = fuzzy->y;
    fuzzy->inp.max_w = fuzzy->w;

    sort_entries(fuzzy);

    if (fuzzy->selected < fuzzy->scroll.line) {
        fuzzy->scroll.line = fuzzy->selected;
    } else if (fuzzy->selected >= fuzzy->scroll.line + fuzzy->h) {
        fuzzy->scroll.line = fuzzy->selected - fuzzy->h + 1;
    }

    if ((line_t) fuzzy->h >= fuzzy->num_entries) {
        fuzzy->scroll.line = 0;
    }

    render_entries(fuzzy);
    render_input(&fuzzy->inp);
}

int send_to_fuzzy(struct fuzzy *fuzzy, int c)
{
    int             r;

    switch (c) {
    case KEY_HOME:
        fuzzy->selected = 0;
        break;

    case KEY_END:
        if (fuzzy->num_entries == 0) {
            fuzzy->selected = 0;
        } else {
            fuzzy->selected = fuzzy->num_entries - 1;
        }
        break;

    case KEY_UP:
        if (fuzzy->selected > 0) {
            fuzzy->selected--;
        }
        break;

    case KEY_DOWN:
        if (fuzzy->selected + 1 < fuzzy->num_entries) {
            fuzzy->selected++;
        }
        break;

    case '\t':
        if (fuzzy->selected + 1 >= fuzzy->num_entries) {
            fuzzy->selected = 0;
        } else {
            fuzzy->selected++;
        }
        break;

    case KEY_BTAB:
        if (fuzzy->num_entries == 0) {
            break;
        }
        if (fuzzy->selected == 0) {
            fuzzy->selected = fuzzy->num_entries - 1;
        } else {
            fuzzy->selected--;
        }
        break;

    case '\n':
        if (fuzzy->num_entries == 0) {
            return INP_CANCELLED;
        }
        return INP_FINISHED;

    default:
        r = send_to_input(&fuzzy->inp, c);
        if (r == INP_CHANGED) {
            fuzzy->selected = 0;
        }
        if (r <= INP_FINISHED) {
            return r;
        }
    }
    return INP_NOTHING;
}
