#ifndef FUZZY_H
#define FUZZY_H

#include <stddef.h>

/**
 * Shows a dialog window with a search bar and display the given entries within
 * the window.
 *
 * The return value shall not be freed and must be duplicated.
 *
 * The user can type input and hit enter to confirm a selection.
 *
 * @param entries       Entries to look through.
 * @param num_entries   Number of entries.
 *
 * @return Selected item or `SIZE_MAX` if no item was selected.
 */
char *choose_fuzzy(const char *dir);

#endif
