#ifndef FUZZY_H
#define FUZZY_H

#include <stddef.h>

/**
 * Shows a dialog window with a search bar and display the given entries within
 * the window.
 *
 * The user can type input and hit enter to confirm a selection.
 *
 * @param entries       Entries to look through.
 * @param num_entries   Number of entries.
 *
 * @return Selected item or `SIZE_MAX` if no item was selected.
 */
size_t choose_fuzzy(const char **entries, size_t num_entries);

struct file_list {
    /// all paths
    char **paths;
    /// allocated space in `paths`
    size_t a;
    /// number of paths
    size_t num;
    /// current path
    char *path;
    /// length of the path
    size_t path_len;
    /// number of allocated bytes in `path`
    size_t a_path;
};

/**
 * Initializes a file list.
 *
 * @param list  The list to initialize.
 * @param root  The initial directory.
 */
void init_file_list(struct file_list *list, const char *root);

/**
 * Gets all files starting from the current directory.
 *
 * @param list  Destination of all paths.
 *
 * @return -1 if there was a system error, 0 otherwise.
 */
int get_deep_files(struct file_list *list);

/**
 * Frees all resources associated to this file list.
 *
 * @param list  List whose associated resources to free.
 */
void clear_file_list(struct file_list *list);

#endif
