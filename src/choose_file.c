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

struct fuzzy ChooseFile;
char *last_dir;

static void update_files(void)
{
    DIR             *dir;
    line_t          i;
    struct dirent   *ent;

    ChooseFile.inp.s[ChooseFile.inp.prefix - 1] = '\0';
    dir = opendir(ChooseFile.inp.s);
    ChooseFile.inp.s[ChooseFile.inp.prefix - 1] = '/';
    if (dir == NULL) {
        return;
    }
 
    for (i = 0; i < ChooseFile.num_entries; i++) {
        free(ChooseFile.entries[i].name);
    }
    ChooseFile.num_entries = 0;
    while (ent = readdir(dir), ent != NULL) {
        if (ChooseFile.num_entries == ChooseFile.a_entries) {
            ChooseFile.a_entries *= 2;
            ChooseFile.a_entries++;
            ChooseFile.entries = xreallocarray(ChooseFile.entries, ChooseFile.a_entries,
                    sizeof(*ChooseFile.entries));
        }
        ChooseFile.entries[ChooseFile.num_entries].type = ent->d_type;
        ChooseFile.entries[ChooseFile.num_entries].name = xstrdup(ent->d_name);
        ChooseFile.num_entries++;
    }
    closedir(dir);
}

static void go_back_one_dir(void)
{
    size_t          index;

    index = ChooseFile.inp.prefix;
    index--;
    while (index > 0) {
        if (ChooseFile.inp.s[index - 1] == '/') {
            break;
        }
        index--;
    }
    if (ChooseFile.inp.s[index] == '.') {
        if (ChooseFile.inp.s[index + 1] == '/') {
            insert_input_prefix(&ChooseFile.inp, ".", index);
        } else if (ChooseFile.inp.s[index + 1] == '.' &&
                   ChooseFile.inp.s[index + 2] == '/') {
            insert_input_prefix(&ChooseFile.inp, "../", index);
        } else {
            ChooseFile.inp.n      = index;
            ChooseFile.inp.index  = index;
            ChooseFile.inp.prefix = index;
        }
    } else {
        ChooseFile.inp.n     = index;
        ChooseFile.inp.index = index;
        ChooseFile.inp.prefix = index;
    }
    update_files();
}

static void move_into_dir(const char *name)
{
    if (name[0] == '.') {
        if (name[1] == '\0') {
            ChooseFile.inp.n     = ChooseFile.inp.prefix;
            ChooseFile.inp.index = ChooseFile.inp.prefix;
            return;
        }
        if (name[1] == '.' && name[2] == '\0') {
            go_back_one_dir();
            return;
        }
    }
    insert_input_prefix(&ChooseFile.inp, name, ChooseFile.inp.prefix);
    insert_input_prefix(&ChooseFile.inp, "/", ChooseFile.inp.prefix);
    update_files();
}

char *choose_file(const char *dir)
{
    char            *rel_dir;
    int             c;
    struct entry    *entry;
    size_t          dir_len;

    ChooseFile.selected = 0;
 
    if (dir == NULL) {
        if (last_dir == NULL) {
            set_input_text(&ChooseFile.inp, "./", 2);
        } else {
            set_input_text(&ChooseFile.inp, last_dir, strlen(last_dir));
        }
    } else {
        rel_dir = get_relative_path(dir);
        set_input_text(&ChooseFile.inp, "/", 1);
        insert_input_prefix(&ChooseFile.inp, rel_dir, 0);
        if (rel_dir[0] != '.') {
            insert_input_prefix(&ChooseFile.inp, "./", 0);
        }
        free(rel_dir);
    }
    
    /* do not use a history */
    set_input_history(&ChooseFile.inp, NULL, 0);

    update_files();

    while (1) {
        render_fuzzy(&ChooseFile);
        c = get_ch();
        switch (c) {
        case '\n':
            if (ChooseFile.num_entries == 0) {
                return NULL;
            }
            entry = &ChooseFile.entries[ChooseFile.selected];
            dir_len = ChooseFile.inp.prefix;
            if (entry->type == DT_DIR) {
                move_into_dir(entry->name);
            } else {
                insert_input_prefix(&ChooseFile.inp, entry->name,
                                    ChooseFile.inp.prefix);
                terminate_input(&ChooseFile.inp);
                free(last_dir);
                last_dir = xmemdup(ChooseFile.inp.s, dir_len + 1);
                last_dir[dir_len] = '\0';
                return ChooseFile.inp.s;
            }
            break;

        case '/':
            if (ChooseFile.num_entries == 0) {
                break;
            }
            entry = &ChooseFile.entries[0];
            if (entry->type != DT_DIR || entry->score == 0) {
                break;
            }
            move_into_dir(entry->name);
            break;

        case CONTROL('D'):
            ChooseFile.inp.s[ChooseFile.inp.prefix - 1] = '\0';
            chdir(ChooseFile.inp.s);
            ChooseFile.inp.s[ChooseFile.inp.prefix - 1] = '/';
            ChooseFile.inp.s[0] = '.';
            ChooseFile.inp.s[1] = '/';
            ChooseFile.inp.index -= ChooseFile.inp.prefix;
            ChooseFile.inp.n     -= ChooseFile.inp.prefix;
            memmove(&ChooseFile.inp.s[0],
                    &ChooseFile.inp.s[ChooseFile.inp.prefix],
                    ChooseFile.inp.n);
            ChooseFile.inp.prefix = 2;
            ChooseFile.inp.index += ChooseFile.inp.prefix;
            ChooseFile.inp.n     += ChooseFile.inp.prefix;
            break;

        case KEY_BACKSPACE:
        case '\x7f':
        case '\b':
            if (ChooseFile.inp.n == ChooseFile.inp.prefix) {
                go_back_one_dir();
                break;
            }
            /* fall through */
        default:
            switch (send_to_fuzzy(&ChooseFile, c)) {
            case INP_CANCELLED: return NULL;
            case INP_FINISHED: return ChooseFile.inp.s;
            }
        }
    }
}
