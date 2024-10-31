#ifndef FUZZY_H
#define FUZZY_H

#include "input.h"
#include "util.h"

#include <stddef.h>

extern struct fuzzy {
    int x, y, w, h;
    size_t selected;
    size_t scroll;
    struct entry {
        int type;
        int score;
        char *name;
    } *entries;
    size_t num_entries;
    struct input inp;
} Fuzzy;

/**
 * Matches the entries in `entries` against the search pattern in the input
 * and sorts the entries such that the matching elements come first.
 *
 * @param fuzzy The fuzzy dialog whose entries to sort.
 */
void sort_entries(struct fuzzy *fuzzy);

/**
 * Renders given fuzzy dialog onto the screen.
 *
 * @param fuzzy The fuzzy dialog to render.
 */
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
 * Clears all resources associated with the fuzzy data structure.
 *
 * @param fuzzy The fuzzy data structure.
 */
void clear_fuzzy(struct fuzzy *fuzzy);

/**
 * Clears all resources besides the entry names.
 *
 * @param fuzzy The fuzzy data structure.
 */
void clear_shallow_fuzzy(struct fuzzy *fuzzy);

/**
 * Choose a file within given directory.
 *
 * @param dir   The directory to start in.
 *
 * @return The path of the file, the caller must free this.
 */
char *choose_file(const char *dir);

#endif
