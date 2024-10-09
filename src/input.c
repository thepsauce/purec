#include "input.h"
#include "util.h"
#include "xalloc.h"

#include <limits.h>
#include <ncurses.h>
#include <string.h>

static void grow_input(struct input *inp, size_t to)
{
    if (inp->a < to) {
        inp->a = to;
        inp->s = xrealloc(inp->s, inp->a);
    }
}

void set_input_text(struct input *inp, const char *text, size_t prefix_len)
{
    inp->n = strlen(text);
    inp->index = inp->n;
    grow_input(inp, inp->n);
    memcpy(inp->s, text, inp->n);

    inp->prefix = prefix_len;

    inp->remember_len = inp->n;
    inp->remember     = xrealloc(inp->remember, inp->remember_len);
    memcpy(inp->remember, inp->s, inp->remember_len);
}

void set_input_history(struct input *inp, char **hist, size_t num_hist)
{
    inp->history       = hist;
    inp->num_history   = num_hist;
    inp->history_index = num_hist;
}

void insert_input_prefix(struct input *inp, const char *text, size_t index)
{
    size_t len;

    len = strlen(text);
    grow_input(inp, inp->prefix + len);
    memmove(&inp->s[index + len],
            &inp->s[index],
            inp->prefix - index);
    memcpy(&inp->s[index], text, len);
    inp->prefix += len;
    inp->n     = inp->prefix;
    inp->index = inp->prefix;
}

void terminate_input(struct input *inp)
{
    grow_input(inp, inp->n + 1);
    inp->s[inp->n] = '\0';
}

static void move_over_utf8(struct input *inp, int dir)
{
    struct glyph g;
    size_t i;

    if (dir > 0) {
        if (inp->index == inp->n) {
            return;
        }
        (void) get_glyph(&inp->s[inp->index], inp->n - inp->index, &g);
        inp->index += g.n;
    } else {
        if (inp->index == inp->prefix) {
            return;
        }
        i = inp->index;
        while (i--, i > inp->prefix) {
            if ((inp->s[i] & 0xc0) != 0x80) {
                break;
            }
        }
        (void) get_glyph(&inp->s[i], inp->n - i, &g);
        if (i + g.n != inp->index) {
            inp->index--;
        } else {
            inp->index = i;
        }
    }
}

char *send_to_input(struct input *inp, int c)
{
    char *h;
    size_t index;

    switch (c) {
    case KEY_HOME:
        inp->index = inp->prefix;
        break;

    case KEY_END:
        inp->index = inp->n;
        break;

    case KEY_LEFT:
        if (inp->index == inp->prefix) {
            break;
        }
        move_over_utf8(inp, -1);
        break;

    case KEY_RIGHT:
        if (inp->index == inp->n) {
            break;
        }
        move_over_utf8(inp, 1);
        break;

    case KEY_UP:
        for (index = inp->history_index; index > 0; index--) {
            h = inp->history[index - 1];
            if (strncmp(h, inp->s, inp->index) == 0) {
                break;
            }
        }

        if (index == 0) {
            break;
        }

        if (inp->history_index == inp->num_history) {
            free(inp->remember);
            inp->remember = xmemdup(inp->s, inp->n);
            inp->remember_len = inp->n;
        }
        h = inp->history[index - 1];
        inp->history_index = index - 1;
        inp->n = strlen(h);
        memcpy(inp->s, h, inp->n);
        break;

    case KEY_DOWN:
        for (index = inp->history_index + 1; index < inp->num_history; index++) {
            h = inp->history[index];
            if (strncmp(h, inp->s, inp->index) == 0) {
                break;
            }
        }

        if (index + 1 >= inp->num_history) {
            memcpy(inp->s, inp->remember, inp->remember_len);
            inp->n = inp->remember_len;
            inp->index = inp->n;
            inp->history_index = inp->num_history;
        } else {
            h = inp->history[index];
            inp->history_index = index;
            inp->n = strlen(h);
            memcpy(inp->s, h, inp->n);
        }
        break;

    case KEY_BACKSPACE:
    case '\x7f':
    case '\b':
        if (inp->index == inp->prefix) {
            break;
        }
        index = inp->index;
        move_over_utf8(inp, -1);
        memmove(&inp->s[inp->index],
                &inp->s[index],
                inp->n - index);
        inp->n -= index - inp->index;
        break;

    case KEY_DC:
        if (inp->index == inp->n) {
            break;
        }
        index = inp->index;
        move_over_utf8(inp, 1);
        memmove(&inp->s[index],
                &inp->s[inp->index],
                inp->n - inp->index);
        inp->n -= inp->index - index;
        inp->index = index;
        break;

    case '\x1b':
    case CONTROL('C'):
        if (inp->a == 0) {
            inp->a = 16;
            inp->s = xrealloc(inp->s, inp->a);
        }
        inp->s[0] = '\n';
        return inp->s;

    default:
        if (c < ' ' || c >= 0x100) {
            break;
        }
        grow_input(inp, inp->n + 1);
        memmove(&inp->s[inp->index + 1],
                &inp->s[inp->index], inp->n - inp->index);
        inp->s[inp->index++] = c;
        inp->n++;
    }

    if (c != '\n') {
        return NULL;
    }
    terminate_input(inp);
    return &inp->s[inp->prefix];
}

void render_input(struct input *inp)
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
        if (i >= inp->index) {
            cur_x = w;
            break;
        }
        (void) get_glyph(&inp->s[i], inp->n - i, &g);
        w += g.w;
        i += g.n;
    }
    
    /* show the prefix */
    if (inp->index == inp->prefix) {
        inp->scroll = 0;
    }

    /* adjust the scrolling so that the cursor is visible */
    if (cur_x < inp->scroll) {
        inp->scroll = cur_x;
    } else if (cur_x >= inp->scroll + inp->max_w) {
        inp->scroll = cur_x - inp->max_w + 1;
    }

    /* render the text */
    move(inp->y, inp->x);
    w = 0;
    for (size_t i = 0; i < inp->n; ) {
        broken = get_glyph(&inp->s[i], inp->n - i, &g) == -1;
        if (w + g.w > inp->scroll + inp->max_w) {
            break;
        }
        if (w >= inp->scroll) {
            if (broken) {
                addch('?' | A_REVERSE);
            } else {
                addnstr(&inp->s[i], g.n);
            }
        }
        i += g.n;
        /* adjust for the case a multi width glyph is only half visible */
        if (w < inp->scroll && w + g.w > inp->scroll) {
            inp->scroll = w + g.w;
        }
        w += g.w;
    }
    /* erase to end of line */
    for (; w < inp->scroll + inp->max_w; w++) {
        addch(' ');
    }

    /* set the visual cursor position */
    move(inp->y, inp->x + cur_x - inp->scroll);
}
