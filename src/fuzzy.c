#include "color.h"
#include "fuzzy.h"
#include "input.h"
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
    const char **s_entries;
    size_t num_s_entries;
    struct fuzzy_entry *entries;
    size_t num_entries;
} Fuzzy;

struct fuzzy_entry {
    /// index within `s_entries` (entry name)
    size_t index;
    /// what the beginning of the fuzzy pattern matched against
    size_t first_match_i;
    /// fuzzy matching score
    int score;
};

static int compare_entries(const void *v_a, const void *v_b)
{
    const struct fuzzy_entry *a, *b;

    a = v_a;
    b = v_b;
    if (a->score != b->score) {
        return b->score - a->score;
    }
    if (a->first_match_i != b->first_match_i) {
        return a->first_match_i - b->first_match_i;
    }
    return strcasecmp(Fuzzy.s_entries[a->index], Fuzzy.s_entries[b->index]);
}

/**
 * Render all entries, this also clears the fuzzy window.
 */
static void render_entries(void)
{
    size_t i, e;
    const char *s;
    int x;
    size_t s_i;
    size_t pat_i;

    set_highlight(stdscr, HI_FUZZY);

    e = MIN(Fuzzy.num_entries, (size_t) (Fuzzy.scroll.line + Fuzzy.h - 2));
    for (i = Fuzzy.scroll.line; i < e; i++) {
        move(Fuzzy.y + 1 + i - Fuzzy.scroll.line, Fuzzy.x);

        if (Fuzzy.selected == i) {
            attr_on(A_REVERSE, NULL);
        } else {
            attr_off(A_REVERSE, NULL);
        }
        s = Fuzzy.s_entries[Fuzzy.entries[i].index];
        x = 0;
        s_i = 0;
        pat_i = Input.prefix;
        while (s[s_i] != '\0' && x + 1 < Fuzzy.w) {
            if (pat_i != Input.len &&
                        tolower(s[s_i]) == tolower(Input.buf[pat_i])) {
                attr_on(A_BOLD, NULL);
                addch(s[s_i]);
                attr_off(A_BOLD, NULL);
                pat_i++;
                x++;
            } else {
                addch(s[s_i]);
            }
            s_i++;
        }

        /* erase to end of line */
        while (getcurx(stdscr) < Fuzzy.x + Fuzzy.w) {
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
 * Matches the entries in `s_entries` against the search pattern in the input
 * and sets (`entries`, `num_entries`) to all matching entries.
 */
static void filter_entries(void)
{
    const char *s;
    size_t s_i;
    size_t pat_i;
    size_t first_i;
    int score;
    int cons_score;
    struct fuzzy_entry *entry;

    Fuzzy.num_entries = 0;
    for (size_t i = 0; i < Fuzzy.num_s_entries; i++) {
        s = Fuzzy.s_entries[i];
        s_i = 0;
        score = 0;
        pat_i = Input.prefix;
        while (pat_i != Input.len && s[s_i] != '\0') {
            if (tolower(Input.buf[pat_i]) == tolower(s[s_i])) {
                if (score == 0) {
                    first_i = s_i;
                }
                /* many consecutive matching letters give a bigger score */
                score += cons_score;
                /* additional point if the case matches */
                if (Input.buf[pat_i] == s[s_i]) {
                    score++;
                }
                cons_score++;
                pat_i++;
            } else {
                cons_score = 1;
            }
            s_i++;
        }

        if (pat_i == Input.len) {
            entry = &Fuzzy.entries[Fuzzy.num_entries++];
            entry->index = i;
            entry->first_match_i = first_i;
            entry->score = score;
        }
    }

    if (Fuzzy.num_entries == 0) {
        Fuzzy.selected = 0;
        return;
    }

    qsort(Fuzzy.entries, Fuzzy.num_entries, sizeof(*Fuzzy.entries),
            compare_entries);
    Fuzzy.selected = MIN(Fuzzy.num_entries - 1, Fuzzy.selected);
}

size_t choose_fuzzy(const char **entries, size_t num_entries)
{
    int c;
    char *s = NULL;

    Fuzzy.x = COLS / 6;
    Fuzzy.y = LINES / 6;
    Fuzzy.w = COLS == 1 ? 1 : COLS * 2 / 3;
    Fuzzy.h = LINES <= 2 ? LINES : LINES * 2 / 3;

    Fuzzy.selected = 0;

    Fuzzy.s_entries = entries;
    Fuzzy.num_s_entries = num_entries;

    Fuzzy.entries = xreallocarray(Fuzzy.entries, num_entries,
            sizeof(*Fuzzy.entries));

    set_input(Fuzzy.x, Fuzzy.y, Fuzzy.w, "> ", 2, NULL, 0);
    do {
        filter_entries();

        if (Fuzzy.selected < Fuzzy.scroll.line) {
            Fuzzy.scroll.line = Fuzzy.selected;
        } else if (Fuzzy.selected + 2 >= Fuzzy.scroll.line + Fuzzy.h) {
            Fuzzy.scroll.line = Fuzzy.selected - Fuzzy.h + 3;
        }

        if ((size_t) Fuzzy.h >= Fuzzy.num_entries) {
            Fuzzy.scroll.line = 0;
        }

        render_entries();
        render_input();
        c = getch();
        switch (c) {
        case KEY_UP:
            if (Fuzzy.selected > 0) {
                Fuzzy.selected--;
            }
            break;

        case KEY_DOWN:
            if (Fuzzy.selected + 1 < num_entries) {
                Fuzzy.selected++;
            }
            break;

        case '\t':
            if (Fuzzy.selected + 1 >= num_entries) {
                Fuzzy.selected = 0;
            } else {
                Fuzzy.selected++;
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

        default:
            s = send_to_input(c);
        }
    } while (s == NULL);

    if (s[0] == '\n' || Fuzzy.num_entries == 0) {
        return SIZE_MAX;
    }

    return Fuzzy.entries[Fuzzy.selected].index;
}

void init_file_list(struct file_list *list, const char *root)
{
    list->paths = NULL;
    list->a = 0;
    list->num = 0;
    list->path = xstrdup(root);
    list->path_len = strlen(root);
    /* +1 for the null terminator */
    list->a_path = list->path_len + 1;
}

int get_deep_files(struct file_list *list)
{
    DIR *dir;
    struct dirent *ent;
    size_t name_len;
    char *path;

    dir = opendir(list->path);
    if (dir == NULL) {
        return -1;
    }
    while (ent = readdir(dir), ent != NULL) {
        /* ignore hidden directories */
        if (ent->d_type == DT_DIR && ent->d_name[0] == '.') {
            continue;
        }
        name_len = strlen(ent->d_name);
        if (ent->d_type == DT_REG) {
            if (list->num == list->a) {
                list->a *= 2;
                list->a++;
                list->paths = xreallocarray(list->paths, list->a,
                        sizeof(*list->paths));
            }

            path = xmalloc(list->path_len + 1 + name_len + 1);
            memcpy(&path[0], list->path, list->path_len);
            path[list->path_len] = '/';
            strcpy(&path[list->path_len + 1], ent->d_name);

            list->paths[list->num++] = path;
        } else if (ent->d_type == DT_DIR) {
            if (list->path_len + name_len + 1 >= list->a_path) {
                list->a_path *= 2;
                list->a_path += name_len + 1;
                list->path = xrealloc(list->path, list->a_path);
            }
            list->path[list->path_len] = '/';
            strcpy(&list->path[list->path_len + 1], ent->d_name);

            list->path_len += name_len + 1;
            (void) get_deep_files(list);
            list->path_len -= name_len + 1;
        }
    }
    closedir(dir);
    return 0;
}

void clear_file_list(struct file_list *list)
{
    for (size_t i = 0; i < list->num; i++) {
        free(list->paths[i]);
    }
    free(list->paths);
    free(list->path);
}
