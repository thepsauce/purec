#include "input.h"
#include "util.h"
#include "xalloc.h"

#include <limits.h>
#include <ncurses.h>
#include <string.h>

struct input Input;

static void grow_input(size_t to)
{
    if (Input.a < to) {
        Input.a = to;
        Input.s = xrealloc(Input.s, Input.a);
    }
}

void set_input_text(const char *text, size_t prefix_len)
{
    Input.n = strlen(text);
    Input.index = Input.n;
    grow_input(Input.n);
    memcpy(Input.s, text, Input.n);

    Input.prefix = prefix_len;

    Input.remember_len = Input.n;
    Input.remember     = xrealloc(Input.remember, Input.remember_len);
    memcpy(Input.remember, Input.s, Input.remember_len);
}

void set_input_history(char **hist, size_t num_hist)
{
    Input.history       = hist;
    Input.num_history   = num_hist;
    Input.history_index = num_hist;
}

void insert_input_prefix(const char *text, size_t index)
{
    size_t len;

    len = strlen(text);
    grow_input(Input.prefix + len);
    memmove(&Input.s[index + len],
            &Input.s[index],
            Input.prefix - index);
    memcpy(&Input.s[index], text, len);
    Input.prefix += len;
    Input.n     = Input.prefix;
    Input.index = Input.prefix;
}

void terminate_input(void)
{
    grow_input(Input.n + 1);
    Input.s[Input.n] = '\0';
}

static void move_over_utf8(int dir)
{
    struct glyph g;
    size_t i;

    if (dir > 0) {
        if (Input.index == Input.n) {
            return;
        }
        (void) get_glyph(&Input.s[Input.index], Input.n - Input.index, &g);
        Input.index += g.n;
    } else {
        if (Input.index == Input.prefix) {
            return;
        }
        i = Input.index;
        while (i--, i > Input.prefix) {
            if ((Input.s[i] & 0xc0) != 0x80) {
                break;
            }
        }
        (void) get_glyph(&Input.s[i], Input.n - i, &g);
        if (i + g.n != Input.index) {
            Input.index--;
        } else {
            Input.index = i;
        }
    }
}

char *send_to_input(int c)
{
    char *h;
    size_t index;

    switch (c) {
    case KEY_HOME:
        Input.index = Input.prefix;
        break;

    case KEY_END:
        Input.index = Input.n;
        break;

    case KEY_LEFT:
        if (Input.index == Input.prefix) {
            break;
        }
        move_over_utf8(-1);
        break;

    case KEY_RIGHT:
        if (Input.index == Input.n) {
            break;
        }
        move_over_utf8(1);
        break;

    case KEY_UP:
        for (index = Input.history_index; index > 0; index--) {
            h = Input.history[index - 1];
            if (strncmp(h, Input.s, Input.index) == 0) {
                break;
            }
        }

        if (index == 0) {
            break;
        }

        if (Input.history_index == Input.num_history) {
            free(Input.remember);
            Input.remember = xmemdup(Input.s, Input.n);
            Input.remember_len = Input.n;
        }
        h = Input.history[index - 1];
        Input.history_index = index - 1;
        Input.n = strlen(h);
        memcpy(Input.s, h, Input.n);
        break;

    case KEY_DOWN:
        for (index = Input.history_index + 1; index < Input.num_history; index++) {
            h = Input.history[index];
            if (strncmp(h, Input.s, Input.index) == 0) {
                break;
            }
        }

        if (index + 1 >= Input.num_history) {
            memcpy(Input.s, Input.remember, Input.remember_len);
            Input.n = Input.remember_len;
            Input.index = Input.n;
            Input.history_index = Input.num_history;
        } else {
            h = Input.history[index];
            Input.history_index = index;
            Input.n = strlen(h);
            memcpy(Input.s, h, Input.n);
        }
        break;

    case KEY_BACKSPACE:
    case '\x7f':
    case '\b':
        if (Input.index == Input.prefix) {
            break;
        }
        index = Input.index;
        move_over_utf8(-1);
        memmove(&Input.s[Input.index],
                &Input.s[index],
                Input.n - index);
        Input.n -= index - Input.index;
        break;

    case KEY_DC:
        if (Input.index == Input.n) {
            break;
        }
        index = Input.index;
        move_over_utf8(1);
        memmove(&Input.s[index],
                &Input.s[Input.index],
                Input.n - Input.index);
        Input.n -= Input.index - index;
        Input.index = index;
        break;

    case '\x1b':
    case CONTROL('C'):
        Input.s[0] = '\n';
        return Input.s;

    default:
        if (c < ' ' || c >= 0x100) {
            break;
        }
        grow_input(Input.n + 1);
        memmove(&Input.s[Input.index + 1],
                &Input.s[Input.index], Input.n - Input.index);
        Input.s[Input.index++] = c;
        Input.n++;
    }

    if (c != '\n') {
        return NULL;
    }
    terminate_input();
    return &Input.s[Input.prefix];
}

void render_input(void)
{
    size_t cur_x = 0;
    size_t w;
    struct glyph g;
    bool broken;

    /* measure out the cursor x position */
    w = 0;
    for (size_t i = 0;; ) {
        /* impossible edge case:
         * if invalid utf8 was inserted and then one byte is deleted,
         * the index might end up in the middle of a multi byte
         */
        if (i >= Input.index) {
            cur_x = w;
            break;
        }
        (void) get_glyph(&Input.s[i], Input.n - i, &g);
        w += g.w;
        i += g.n;
    }
    
    /* show the prefix */
    if (Input.index == Input.prefix) {
        Input.scroll = 0;
    }

    /* adjust the scrolling so that the cursor is visible */
    if (cur_x < Input.scroll) {
        Input.scroll = cur_x;
    } else if (cur_x >= Input.scroll + Input.max_w) {
        Input.scroll = cur_x - Input.max_w + 1;
    }

    /* render the text */
    move(Input.y, Input.x);
    w = 0;
    for (size_t i = 0; i < Input.n; ) {
        broken = get_glyph(&Input.s[i], Input.n - i, &g) == -1;
        if (w + g.w > Input.scroll + Input.max_w) {
            break;
        }
        if (w >= Input.scroll) {
            if (broken) {
                addch('?' | A_REVERSE);
            } else {
                addnstr(&Input.s[i], g.n);
            }
        }
        i += g.n;
        /* adjust for the case a multi width glyph is only half visible */
        if (w < Input.scroll && w + g.w > Input.scroll) {
            Input.scroll = w + g.w;
        }
        w += g.w;
    }
    /* erase to end of line */
    for (; w < Input.scroll + Input.max_w; w++) {
        addch(' ');
    }

    /* set the visual cursor position */
    move(Input.y, Input.x + cur_x - Input.scroll);
}
