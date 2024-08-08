#ifndef INPUT_H
#define INPUT_H

#include <stdlib.h>

/* ** ** ** * * ** * ** *
 *         Input        *
 * * *** * ** * * * *** */

/**
 * The input module is a lot like GNU readline but with specialized
 * functionality.
 *
 * Example usage of the entire input module:
 * ```C
 * const char *get_input(void)
 * {
 *     static char **history;
 *     static size_t num_history;
 *
 *     const int x = 0;
 *     const int y = 0;
 *
 *     char *s;
 *
 *     set_input(x, y, COLS, "Enter: ", sizeof("Enter:"), history, num_history);
 *
 *     while (render_input(), s = send_to_input(getch()), s == NULL) {
 *         (void) 0;
 *     }
 *
 *     if (s[0] == '\0') {
 *         return;
 *     }
 *
 *     history = xreallocarray(history, num_history + 1, sizeof(*history));
 *     history[num_history++] = xstrdup(&s[-Input.prefix]);
 *
 *     return s;
 * }
 * ```
 * Now `get_input()` can be called to let the user input text, the user can also
 * move through the history using the arrow keys (up and down).
 *
 * More examples can be found in the source itself in `cmd.c` (command line
 * style) and `file.c` (no history, simple fuzzy search).
 */

extern struct input {
    /// x position of the input
    int x;
    /// y position of the input
    int y;
    /// maximum width
    int max_w;
    /// size of the unmodifiable prefix
    size_t prefix;
    /// current command line content
    char *buf;
    /// number of allocated bytes for `buf`
    size_t a;
    /// length of the buffer
    size_t len;
    /// index within the buffer
    size_t index;
    /// horizontal scrolling
    size_t scroll;
    /// history of all entered linees
    char **history;
    /// number of lines in the history
    size_t num_history;
    /// current index within the history
    size_t history_index;
    /**
     * Remembered text, used when moving through history to save the original
     * text.
     */
    char *remember;
    /// Length of remembered text.
    size_t remember_len;
} Input;

/**
 * Sets up an input.
 *
 * The given `hist` pointer will be modified after a successful command line was
 * entered.
 *
 * @param x         X position of the input box.
 * @param y         Y position of the input box.
 * @param max_w     Width of the input box.
 * @param inp       Initial input.
 * @param prefix    Size of the text segment in front of the input.
 * @param hist      History entries.
 * @param num_hist  Number of entries in the history.
 */
void set_input(int x, int y, int max_w, const char *inp, size_t prefix,
        char **hist, size_t num_hist);

/**
 * Let the input box handle user input.
 *
 * If enter is pressed and there is input, the input is added to the given
 * history and a null terminator is added to the end for convenience.
 *
 * @param c User input.
 *
 * @return An empty string if ESCAPE was pressed, the input if ENTER was pressed
 *         or NULL otherwise.
 */
char *send_to_input(int c);

/**
 * Renders the input at its defined position, considering scroll and placing the
 * cursor.
 */
void render_input(void);

#endif
