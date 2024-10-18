#ifndef FUZZY_H
#define FUZZY_H

#include "input.h"
#include "util.h"

#include <stddef.h>

extern struct fuzzy {
    int x, y, w, h;
    struct pos scroll;
    line_t selected;
    struct entry {
        int type;
        int score;
        char *name;
    } *entries;
    line_t num_entries;
    line_t a_entries;
    struct input inp;
} Fuzzy;

void render_fuzzy(struct fuzzy *fuzzy);

/**
 * Shows a dialog window with a search bar and display the given entries within
 * the window.
 *
 * The return value shall not be freed and must be duplicated.
 *
 * The user can type input and hit enter to confirm a selection.
 *
 * @param fuzzy The fuzzy structure.
 * @param c     The character to handle.
 *
 * @return Selected item or `SIZE_MAX` if no item was selected.
 */
int send_to_fuzzy(struct fuzzy *fuzzy, int c);

/**
 * Choose a file within given directory.
 *
 * @param dir   The directory to start in.
 *
 * @return The path of the file, this points to `Input.s`, no freeing needed.
 */
char *choose_file(const char *dir);

#endif
