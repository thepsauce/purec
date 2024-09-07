#include "color.h"
#include "fuzzy.h"
#include "input.h"
#include "purec.h"
#include "util.h"
#include "xalloc.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include <dirent.h>

struct file_chooser {
    int x, y, w, h;
    struct pos scroll;
    size_t selected;
    struct entry {
        int type;
        int score;
        char *name;
    } *entries;
    size_t num_entries;
    size_t a_entries;
    char *last_dir;
} Fuzzy;

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
 * Render all entries, this also clears the fuzzy window.
 */
static void render_entries(void)
{
    size_t i, e;
    struct entry *entry;
    int x;
    size_t s_i;
    size_t pat_i;

    set_highlight(stdscr, HI_FUZZY);

    e = MIN(Fuzzy.num_entries, (size_t) (Fuzzy.scroll.line + Fuzzy.h));
    for (i = Fuzzy.scroll.line; i < e; i++) {
        move(Fuzzy.y + 1 + i - Fuzzy.scroll.line, Fuzzy.x);

        if (Fuzzy.selected == i) {
            attr_on(A_REVERSE, NULL);
        } else {
            attr_off(A_REVERSE, NULL);
        }
 
        entry = &Fuzzy.entries[i];
        x = 0;
        s_i = 0;
        pat_i = Input.prefix;
        while (entry->name[s_i] != '\0' && x < Fuzzy.w) {
            if (entry->score > 0 && pat_i != Input.n &&
                    tolower(entry->name[s_i]) ==
                        tolower(Input.s[pat_i])) {
                attr_on(A_BOLD, NULL);
                addch(entry->name[s_i]);
                attr_off(A_BOLD, NULL);
                pat_i++;
            } else {
                addch(entry->name[s_i]);
            }
            x++;
            s_i++;
        }

        if (entry->type == DT_DIR && x < Fuzzy.w) {
            addch('/');
            x++;
        }

        /* erase to end of line */
        for (; x < Fuzzy.w; x++) {
            addch(' ');
        }
    }

    /* erase the bottom */
    attr_off(A_REVERSE, NULL);
    for (; i < Fuzzy.scroll.line + Fuzzy.h; i++) {
        move(Fuzzy.y + 1 + i - Fuzzy.scroll.line, Fuzzy.x);
        while (getcurx(stdscr) < Fuzzy.x + Fuzzy.w) {
            addch(' ');
        }
    }
}

/**
 * Matches the entries in `entries` against the search pattern in the input
 * and sorts the entries such that the matching elements come first.
 */
static void sort_entries(void)
{
    size_t s_i;
    size_t pat_i;
    int score;
    int cons_score;
    struct entry *entry;

    if (Fuzzy.num_entries == 0) {
        Fuzzy.selected = 0;
        return;
    }

    for (size_t i = 0; i < Fuzzy.num_entries; i++) {
        entry = &Fuzzy.entries[i];
        s_i = 0;
        score = 0;
        cons_score = 1;
        pat_i = Input.prefix;
        while (pat_i != Input.n && entry->name[s_i] != '\0') {
            if (tolower(Input.s[pat_i]) == tolower(entry->name[s_i])) {
                /* many consecutive matching letters give a bigger score */
                score += cons_score;
                /* additional point if the case matches */
                if (Input.s[pat_i] == entry->name[s_i]) {
                    score++;
                }
                cons_score++;
                pat_i++;
            } else {
                cons_score = 1;
            }
            s_i++;
        }

        if (pat_i == Input.n) {
            entry->score = score;
        } else {
            entry->score = 0;
        }
    }

    qsort(Fuzzy.entries, Fuzzy.num_entries,
          sizeof(*Fuzzy.entries), compare_entries);
    Fuzzy.selected = MIN(Fuzzy.num_entries - 1, Fuzzy.selected);
}

int update_files(void)
{
    DIR *dir;
    struct dirent *ent;

    Input.s[Input.prefix - 1] = '\0';
    dir = opendir(Input.s);
    Input.s[Input.prefix - 1] = '/';
    if (dir == NULL) {
        return -1;
    }
 
    for (size_t i = 0; i < Fuzzy.num_entries; i++) {
        free(Fuzzy.entries[i].name);
    }
    Fuzzy.num_entries = 0;
    while (ent = readdir(dir), ent != NULL) {
        /* ignore hidden directories */
        if (ent->d_type == DT_DIR && ent->d_name[0] == '.') {
            continue;
        }
        if (Fuzzy.num_entries == Fuzzy.a_entries) {
            Fuzzy.a_entries *= 2;
            Fuzzy.a_entries++;
            Fuzzy.entries = xreallocarray(Fuzzy.entries, Fuzzy.a_entries,
                    sizeof(*Fuzzy.entries));
        }
        Fuzzy.entries[Fuzzy.num_entries].type = ent->d_type;
        Fuzzy.entries[Fuzzy.num_entries].name = xstrdup(ent->d_name);
        Fuzzy.num_entries++;
    }
    closedir(dir);
    return 0;
}

char *choose_fuzzy(const char *dir)
{
    int c;
    char *s = NULL;
    struct entry *entry;
    size_t dir_len;

    Fuzzy.selected = 0;
    
    if (Input.a < 1024) {
        Input.a = 1024;
        Input.s = xrealloc(Input.s, Input.a);
    }
 
    if (dir == NULL) {
        if (Fuzzy.last_dir == NULL) {
            set_input_text("./", 2);
        } else {
            set_input_text(Fuzzy.last_dir, strlen(Fuzzy.last_dir));
        }
    } else {
        set_input_text(dir, strlen(dir));
        append_input_prefix("/");
    }
    
    /* do not use a history */
    set_input_history(NULL, 0);

    (void) update_files();

    do {
        Fuzzy.x = COLS / 6;
        Fuzzy.y = LINES / 6;
        Fuzzy.w = COLS == 1 ? 1 : COLS * 2 / 3;
        Fuzzy.h = LINES <= 2 ? LINES : LINES * 2 / 3;

        Input.x = Fuzzy.x;
        Input.y = Fuzzy.y;
        Input.max_w = Fuzzy.w;

        sort_entries();

        if (Fuzzy.selected < Fuzzy.scroll.line) {
            Fuzzy.scroll.line = Fuzzy.selected;
        } else if (Fuzzy.selected >= Fuzzy.scroll.line + Fuzzy.h) {
            Fuzzy.scroll.line = Fuzzy.selected - Fuzzy.h + 1;
        }

        if ((size_t) Fuzzy.h >= Fuzzy.num_entries) {
            Fuzzy.scroll.line = 0;
        }

        render_entries();
        render_input();
        c = get_ch();
        switch (c) {
        case KEY_UP:
            if (Fuzzy.selected > 0) {
                Fuzzy.selected--;
            }
            break;

        case KEY_DOWN:
            if (Fuzzy.selected + 1 < Fuzzy.num_entries) {
                Fuzzy.selected++;
            }
            break;

        case '\t':
            if (Fuzzy.selected + 1 >= Fuzzy.num_entries) {
                Fuzzy.selected = 0;
            } else {
                Fuzzy.selected++;
            }
            break;

        case '\n':
            if (Fuzzy.num_entries == 0) {
                return NULL;
            }
            entry = &Fuzzy.entries[Fuzzy.selected];
            dir_len = Input.prefix;
            append_input_prefix(entry->name);
            if (entry->type == DT_DIR) {
                append_input_prefix("/");
                (void) update_files();
            } else {
                terminate_input();
                free(Fuzzy.last_dir);
                Fuzzy.last_dir = xmemdup(Input.s, dir_len + 1);
                Fuzzy.last_dir[dir_len] = '\0';
                return Input.s;
            }
            break;

        case KEY_BTAB:
            if (Fuzzy.num_entries == 0) {
                break;
            }
            if (Fuzzy.selected == 0) {
                Fuzzy.selected = Fuzzy.num_entries - 1;
            } else {
                Fuzzy.selected--;
            }
            break;

        case '/':
            if (Fuzzy.num_entries == 0) {
                break;
            }
            entry = &Fuzzy.entries[0];
            if (entry->type != DT_DIR || entry->score == 0) {
                break;
            }
            append_input_prefix(entry->name);
            append_input_prefix("/");
            (void) update_files();
            break;

        case KEY_BACKSPACE:
        case '\x7f':
        case '\b':
            if (Input.n == Input.prefix) {
                Input.prefix--;
                while (Input.prefix > 0) {
                    if (Input.s[Input.prefix - 1] == '/') {
                        break;
                    }
                    Input.prefix--;
                }
                if (Input.prefix == 0) {
                    Input.prefix += 2;
                    break;
                }
                Input.n = Input.prefix;
                Input.index = Input.n;
                (void) update_files();
                break;
            }
            /* fall through */
        default:
            s = send_to_input(c);
            Fuzzy.selected = 0;
        }
    } while (s == NULL);
    return NULL;
}
