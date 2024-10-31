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
void sort_entries(struct fuzzy *fuzzy)
{
    char            *cur_entry_name;
    size_t          i;
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

    cur_entry_name = fuzzy->entries[fuzzy->selected].name;
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
    for (i = 0; i < fuzzy->num_entries; i++) {
        if (fuzzy->entries[i].name == cur_entry_name) {
            fuzzy->selected = i;
            break;
        }
    }
}

/**
 * Render all entries, this also clears the fuzzy window.
 */
static void render_entries(struct fuzzy *fuzzy)
{
    size_t          i, e;
    struct entry    *entry;
    int             x;
    size_t          s_i;
    size_t          pat_i;
    struct glyph    g_n, g_p;
    bool            broken;

    e = MIN(fuzzy->num_entries, fuzzy->scroll + fuzzy->h - 4);
    for (i = fuzzy->scroll; i < e; i++) {
        move(fuzzy->y + 3 + i - fuzzy->scroll, fuzzy->x + 1);

        entry = &fuzzy->entries[i];

        if (entry->type == DT_DIR) {
            set_highlight(stdscr, HI_TYPE);
        } else {
            set_highlight(stdscr, HI_NORMAL);
        }

        if (fuzzy->selected == i) {
            attr_on(A_REVERSE, NULL);
        } else {
            attr_off(A_REVERSE, NULL);
        }
 
        x = 0;
        s_i = 0;
        pat_i = fuzzy->inp.prefix;
        while (entry->name[s_i] != '\0') {
            broken = get_glyph(&entry->name[s_i], SIZE_MAX, &g_n) == -1;
            if (x + g_n.w >= fuzzy->w - 2) {
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

        if (entry->type == DT_DIR && x < fuzzy->w - 2) {
            addch('/');
            x++;
        }

        /* erase to end of line */
        for (; x < fuzzy->w - 2; x++) {
            addch(' ');
        }
    }

    /* erase the bottom */
    attr_off(A_REVERSE, NULL);
    for (; i < fuzzy->scroll + fuzzy->h - 4; i++) {
        move(fuzzy->y + 3 + i - fuzzy->scroll, fuzzy->x + 1);
        for (x = fuzzy->x + 1; x < fuzzy->x + fuzzy->w - 2; x++) {
            addch(' ');
        }
    }
}

static void draw_frame(int x, int y, int w, int h)
{
    int             i;

    /* draw border */
    for (i = x + 1; i < x + w - 1; i++) {
        mvaddstr(y, i, "\u2550");
        mvaddstr(y + h - 1, i, "\u2550");
    }
    for (i = y + 1; i < y + h - 1; i++) {
        mvaddstr(i, x, "\u2551");
        mvaddstr(i, x + w - 1, "\u2551");
    }
    /* draw corners */
    mvaddstr(y, x, "\u2554");
    mvaddstr(y, x + w - 1, "\u2557");
    mvaddstr(y + h - 1, x, "\u255a");
    mvaddstr(y + h - 1, x + w - 1, "\u255d");
    /* draw connecting line */
    mvaddstr(y + 2, x, "\u255f");
    for (i = x + 1; i < x + w - 1; i++) {
        mvaddstr(y + 2, i, "\u2500");
    }
    mvaddstr(y + 2, x + w - 1, "\u2562");
}

void render_fuzzy(struct fuzzy *fuzzy)
{
    fuzzy->x = COLS / 6;
    fuzzy->y = LINES / 6;
    fuzzy->w = COLS == 1 ? 1 : COLS * 2 / 3;
    fuzzy->h = LINES <= 2 ? LINES : LINES * 2 / 3;

    set_highlight(stdscr, HI_NORMAL);

    draw_frame(fuzzy->x, fuzzy->y, fuzzy->w, fuzzy->h);
    if (fuzzy->num_entries == 0) {
        mvaddstr(fuzzy->y, fuzzy->x + 2, "(Nothing)");
    } else {
        mvprintw(fuzzy->y, fuzzy->x + 2, "(%zu/%zu)",
                 fuzzy->selected + 1,
                 fuzzy->num_entries);
    }

    fuzzy->inp.x = fuzzy->x + 1;
    fuzzy->inp.y = fuzzy->y + 1;
    fuzzy->inp.max_w = fuzzy->w - 2;

    if (fuzzy->selected < fuzzy->scroll) {
        fuzzy->scroll = fuzzy->selected;
    } else if (fuzzy->selected >= fuzzy->scroll + fuzzy->h - 4) {
        fuzzy->scroll = fuzzy->selected - fuzzy->h + 5;
    }

    if ((size_t) fuzzy->h - 4 >= fuzzy->num_entries) {
        fuzzy->scroll = 0;
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
            sort_entries(fuzzy);
            fuzzy->selected = 0;
        }
        if (r <= INP_FINISHED) {
            return r;
        }
    }
    return INP_NOTHING;
}

void clear_fuzzy(struct fuzzy *fuzzy)
{
    size_t          i;

    for (i = 0; i < fuzzy->num_entries; i++) {
        free(fuzzy->entries[i].name);
    }
    clear_shallow_fuzzy(fuzzy);
}

void clear_shallow_fuzzy(struct fuzzy *fuzzy)
{
    free(fuzzy->entries);
    free(fuzzy->inp.s);
    free(fuzzy->inp.remember);
}
