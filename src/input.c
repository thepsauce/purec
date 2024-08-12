#include "input.h"
#include "util.h"
#include "xalloc.h"

#include <ncurses.h>
#include <string.h>

struct input Input;

void set_input(int x, int y, int max_w, const char *inp, size_t prefix,
        char **hist, size_t num_hist)
{
    Input.x = x;
    Input.y = y;
    Input.max_w = max_w;

    Input.len = strlen(inp);
    Input.index = Input.len;
    if (Input.a < Input.len + 128) {
        Input.a = Input.len + 128;
        Input.buf = xrealloc(Input.buf, Input.a);
    }
    memcpy(Input.buf, inp, Input.len);

    Input.prefix = prefix;

    Input.history = hist;
    Input.num_history = num_hist;
    Input.history_index = num_hist;

    Input.remember_len = Input.prefix;
    Input.remember = xrealloc(Input.remember, Input.remember_len);
    memcpy(Input.remember, Input.buf, Input.remember_len);
}

char *send_to_input(int c)
{
    char ch;
    char *h;
    size_t index;

    switch (c) {
    case KEY_HOME:
        Input.index = Input.prefix;
        break;

    case KEY_END:
        Input.index = Input.len;
        break;

    case KEY_LEFT:
        if (Input.index == Input.prefix) {
            break;
        }
        Input.index--;
        break;

    case KEY_RIGHT:
        if (Input.index == Input.len) {
            break;
        }
        Input.index++;
        break;

    case KEY_UP:
        for (index = Input.history_index; index > 0; index--) {
            h = Input.history[index - 1];
            if (strncmp(h, Input.buf, Input.index) == 0) {
                break;
            }
        }

        if (index == 0) {
            break;
        }

        if (Input.history_index == Input.num_history) {
            free(Input.remember);
            Input.remember = xmemdup(Input.buf, Input.len);
            Input.remember_len = Input.len;
        }
        h = Input.history[index - 1];
        Input.history_index = index - 1;
        Input.len = strlen(h);
        memcpy(Input.buf, h, Input.len);
        break;

    case KEY_DOWN:
        for (index = Input.history_index + 1; index < Input.num_history; index++) {
            h = Input.history[index];
            if (strncmp(h, Input.buf, Input.index) == 0) {
                break;
            }
        }

        if (index + 1 >= Input.num_history) {
            memcpy(Input.buf, Input.remember, Input.remember_len);
            Input.len = Input.remember_len;
            Input.index = Input.len;
            Input.history_index = Input.num_history;
        } else {
            h = Input.history[index];
            Input.history_index = index;
            Input.len = strlen(h);
            memcpy(Input.buf, h, Input.len);
        }
        break;

    case KEY_BACKSPACE:
    case '\x7f':
        if (Input.index == Input.prefix) {
            break;
        }
        memmove(&Input.buf[Input.index - 1],
                &Input.buf[Input.index],
                Input.len - Input.index);
        Input.len--;
        Input.index--;
        break;

    case KEY_DC:
        if (Input.index == Input.len) {
            break;
        }
        Input.len--;
        memmove(&Input.buf[Input.index],
                &Input.buf[Input.index + 1],
                Input.len - Input.index);
        break;

    case '\x1b':
    case CONTROL('C'):
        Input.buf[0] = '\n';
        return Input.buf;

    default:
        ch = c;
        if (c >= 0x100 || ch < ' ') {
            break;
        }
        if (Input.len == Input.a) {
            Input.a *= 2;
            Input.a++;
            Input.buf = xrealloc(Input.buf, Input.a);
        }
        memmove(&Input.buf[Input.index + 1],
                &Input.buf[Input.index], Input.len - Input.index);
        Input.buf[Input.index++] = ch;
        Input.len++;
    }

    if (Input.index < Input.scroll) {
        Input.scroll = Input.index;
        if (Input.scroll <= Input.prefix + 1) {
            Input.scroll = 0;
        } else {
            Input.scroll -= 5;
        }
    } else if (Input.index >= Input.scroll + Input.max_w) {
        Input.scroll = Input.index - Input.max_w + 6;
    }

    if (c != '\n') {
        return NULL;
    }

    /* add null terminator */
    if (Input.len == Input.a) {
        Input.a *= 2;
        Input.a++;
        Input.buf = xrealloc(Input.buf, Input.a);
    }
    Input.buf[Input.len] = '\0';
    return &Input.buf[Input.prefix];
}

void render_input(void)
{
    move(Input.y, Input.x);
    addnstr(&Input.buf[Input.scroll],
            MIN((size_t) Input.max_w, Input.len - Input.scroll));
    /* erase to end of line */
    for (int x = getcurx(stdscr); x < Input.x + Input.max_w; x++) {
        addch(' ');
    }
    move(Input.y, Input.x + Input.index - Input.scroll);
}
