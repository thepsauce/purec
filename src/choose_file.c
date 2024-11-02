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

char *last_dir;

static void update_files(struct fuzzy *fuzzy)
{
    DIR             *dir;
    size_t          i;
    struct dirent   *ent;
    size_t          a;

    fuzzy->inp.s[fuzzy->inp.prefix - 1] = '\0';
    dir = opendir(fuzzy->inp.s);
    fuzzy->inp.s[fuzzy->inp.prefix - 1] = '/';
    if (dir == NULL) {
        return;
    }
 
    for (i = 0; i < fuzzy->num_entries; i++) {
        free(fuzzy->entries[i].name);
    }
    a = fuzzy->num_entries;
    fuzzy->num_entries = 0;
    while (ent = readdir(dir), ent != NULL) {
        if (fuzzy->num_entries == a) {
            a *= 2;
            a++;
            fuzzy->entries = xreallocarray(fuzzy->entries, a,
                                               sizeof(*fuzzy->entries));
        }
        fuzzy->entries[fuzzy->num_entries].type = ent->d_type;
        fuzzy->entries[fuzzy->num_entries].name = xstrdup(ent->d_name);
        fuzzy->num_entries++;
    }
    closedir(dir);

    sort_entries(fuzzy);
    fuzzy->selected = 0;
}

static void go_back_one_dir(struct fuzzy *fuzzy)
{
    size_t          index;
    char            *cwd, *s;
    size_t          i;
    size_t          num_slashes;

    index = fuzzy->inp.prefix;
    index--;
    while (index > 0) {
        if (fuzzy->inp.s[index - 1] == '/') {
            break;
        }
        index--;
    }
    if (fuzzy->inp.s[index] == '.') {
        if (fuzzy->inp.s[index + 1] == '/') {
            insert_input_prefix(&fuzzy->inp, ".", index);
        } else if (fuzzy->inp.s[index + 1] == '.' &&
                   fuzzy->inp.s[index + 2] == '/') {
            /* compares the number of directories moved out of with the
             * number of directories we can actually move out of
             */
            cwd = getcwd(NULL, 0);
            num_slashes = 0;
            for (s = cwd; s[0] != '\0'; s++) {
                if (s[0] == '/') {
                    num_slashes++;
                }
            }
            for (i = 0; i < fuzzy->inp.prefix; i++) {
                if (fuzzy->inp.s[i] == '/') {
                    num_slashes--;
                }
            }
            if (num_slashes > 0) {
                insert_input_prefix(&fuzzy->inp, "../", index);
            }
            free(cwd);
        } else {
            fuzzy->inp.n      = index;
            fuzzy->inp.index  = index;
            fuzzy->inp.prefix = index;
        }
    } else {
        fuzzy->inp.n     = index;
        fuzzy->inp.index = index;
        fuzzy->inp.prefix = index;
    }
    update_files(fuzzy);
}

static void move_into_dir(struct fuzzy *fuzzy, const char *name)
{
    size_t          i;
    size_t          num_dirs;
    char            *cwd, *s, *e;

    if (name[0] == '.') {
        if (name[1] == '\0') {
            fuzzy->inp.n     = fuzzy->inp.prefix;
            fuzzy->inp.index = fuzzy->inp.prefix;
            return;
        }
        if (name[1] == '.' && name[2] == '\0') {
            go_back_one_dir(fuzzy);
            return;
        }
    }

    /* check if the prefix is a repeating sequence of `../` */
    num_dirs = 0;
    for (i = 0; i + 2 < fuzzy->inp.prefix; i += 3) {
        if (fuzzy->inp.s[i] != '.' || fuzzy->inp.s[i + 1] != '.' ||
                fuzzy->inp.s[i + 2] != '/') {
            break;
        }
        num_dirs++;
    }
    if (i == fuzzy->inp.prefix) {
        cwd = getcwd(NULL, 0);
        for (s = &cwd[strlen(cwd)], e = s; s > cwd; s--) {
            if (s[-1] == '/') {
                num_dirs--;
                if (num_dirs == 0) {
                    break;
                }
                e = &s[-1];
            }
        }
        if (strncmp(name, s, e - s) == 0 && name[e - s] == '\0') {
            if (fuzzy->inp.prefix == 3) {
                /* make `../` -> `./` */
                fuzzy->inp.prefix--;
                memmove(&fuzzy->inp.s[0], &fuzzy->inp.s[1], fuzzy->inp.prefix);
            } else {
                /* trim `../` off the beginning */
                fuzzy->inp.prefix -= 3;
                memmove(&fuzzy->inp.s[0], &fuzzy->inp.s[3], fuzzy->inp.prefix);
            }
            /* adjust these values like in `insert_input_prefix()` */
            fuzzy->inp.n = fuzzy->inp.prefix;
            fuzzy->inp.index = fuzzy->inp.prefix;
            update_files(fuzzy);
            free(cwd);
            return;
        }
        free(cwd);
    }

    insert_input_prefix(&fuzzy->inp, name, fuzzy->inp.prefix);
    insert_input_prefix(&fuzzy->inp, "/", fuzzy->inp.prefix);
    update_files(fuzzy);
}

char *choose_file(const char *dir)
{
    struct fuzzy    fuzzy;
    char            *rel_dir;
    int             c;
    struct entry    *entry;
    size_t          dir_len;
    char            *s;
 
    memset(&fuzzy, 0, sizeof(fuzzy));
    if (dir == NULL) {
        if (last_dir == NULL) {
            set_input_text(&fuzzy.inp, "./", 2);
        } else {
            set_input_text(&fuzzy.inp, last_dir, strlen(last_dir));
        }
    } else {
        rel_dir = get_relative_path(dir);
        set_input_text(&fuzzy.inp, "/", 1);
        insert_input_prefix(&fuzzy.inp, rel_dir, 0);
        if (rel_dir[0] != '.') {
            insert_input_prefix(&fuzzy.inp, "./", 0);
        }
        free(rel_dir);
    }
    
    update_files(&fuzzy);
    fuzzy.selected = 0;

    while (1) {
        render_fuzzy(&fuzzy);
        c = get_ch();
        switch (c) {
        case '\n':
            if (fuzzy.num_entries == 0) {
                clear_fuzzy(&fuzzy);
                return NULL;
            }
            entry = &fuzzy.entries[fuzzy.selected];
            dir_len = fuzzy.inp.prefix;
            if (entry->type == DT_DIR) {
                move_into_dir(&fuzzy, entry->name);
            } else {
                insert_input_prefix(&fuzzy.inp, entry->name,
                                    fuzzy.inp.prefix);
                terminate_input(&fuzzy.inp);
                free(last_dir);
                last_dir = xmemdup(fuzzy.inp.s, dir_len + 1);
                last_dir[dir_len] = '\0';
                s = xstrdup(fuzzy.inp.s);
                clear_fuzzy(&fuzzy);
                return s;
            }
            break;

        case '/':
            if (fuzzy.num_entries == 0) {
                break;
            }
            entry = &fuzzy.entries[0];
            if (entry->type != DT_DIR || entry->score == 0) {
                break;
            }
            move_into_dir(&fuzzy, entry->name);
            break;

        case CONTROL('D'):
            fuzzy.inp.s[fuzzy.inp.prefix - 1] = '\0';
            chdir(fuzzy.inp.s);
            fuzzy.inp.s[fuzzy.inp.prefix - 1] = '/';
            fuzzy.inp.s[0] = '.';
            fuzzy.inp.s[1] = '/';
            fuzzy.inp.index -= fuzzy.inp.prefix;
            fuzzy.inp.n     -= fuzzy.inp.prefix;
            memmove(&fuzzy.inp.s[0],
                    &fuzzy.inp.s[fuzzy.inp.prefix],
                    fuzzy.inp.n);
            fuzzy.inp.prefix = 2;
            fuzzy.inp.index += fuzzy.inp.prefix;
            fuzzy.inp.n     += fuzzy.inp.prefix;
            break;

        case KEY_BACKSPACE:
        case '\x7f':
        case '\b':
            if (fuzzy.inp.n == fuzzy.inp.prefix) {
                go_back_one_dir(&fuzzy);
                break;
            }
            /* fall through */
        default:
            switch (send_to_fuzzy(&fuzzy, c)) {
            case INP_CANCELLED:
                clear_fuzzy(&fuzzy);
                return NULL;

            case INP_FINISHED:
                s = xstrdup(fuzzy.inp.s);
                clear_fuzzy(&fuzzy);
                return s;
            }
        }
    }
}
