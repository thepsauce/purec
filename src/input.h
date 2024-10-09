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
 *     static struct input inp;
 *
 *     const int x = 0;
 *     const int y = 0;
 *
 *     char *s;
 *
 *     set_input_text(&inp, "Enter: ", sizeof("Enter:"));
 *     set_input_history(&inp, history, num_history);
 *
 *     while (inp.x = 0, inp.y = 0, inp.max_w = COLS,
 *            render_input(&inp), s = send_to_input(&inp, getch()), s == NULL) {
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

struct input {
    /// x position of the input
    int x;
    /// y position of the input
    int y;
    /// maximum width
    int max_w;
    /// size of the unmodifiable prefix
    size_t prefix;
    /// current input line content
    char *s;
    /// number of allocated bytes for `s`
    size_t a;
    /// length of the input line
    size_t n;
    /// index within the input line
    size_t index;
    /// horizontal scrolling
    size_t scroll;
    /// history of all entered lines
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
};

/**
 * Sets the text of the input line.
 * 
 * @param inp           The input to set the text of.
 * @param text          The text to set it to including the prefix.
 * @param prefix_len    Size of the text segment in front of the input.
 */
void set_input_text(struct input *inp, const char *text, size_t prefix_len);

/**
 * Sets the history of the input line.
 *
 * @param inp       The input whose history to set.
 * @param hist      History entries, may be `NULL` (no history).
 * @param num_hist  Number of entries in the history.
 */
void set_input_history(struct input *inp, char **history, size_t num_hist);

/**
 * Inserts given text into the input line.
 *
 * @param inp   The input to insert a prefix into.
 * @param text  The text to append to the prefix.
 * @param index The index to insert the text into.
 */
void insert_input_prefix(struct input *inp, const char *text, size_t index);

/**
 * Adds a null terminator to the end of the input line.
 *
 * @param inp   The input to terminate.
 */
void terminate_input(struct input *inp);

/**
 * Let the input box handle user input.
 *
 * If enter is pressed and there is input, the input is added to the given
 * history and a null terminator is added to the end for convenience.
 *
 * @param inp   The input to send the character to.
 * @param c     User input.
 *
 * @return An empty string if ESCAPE was pressed, the input if ENTER was pressed
 *         or NULL otherwise.
 */
char *send_to_input(struct input *inp, int c);

/**
 * Renders the input at its defined position, considering scroll and placing the
 * cursor.
 *
 * @param inp   The input to render.
 */
void render_input(struct input *inp);

#endif
