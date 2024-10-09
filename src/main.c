#include "color.h"
#include "frame.h"
#include "purec.h"
#include "xalloc.h"

#include <ctype.h>

int get_ch(void)
{
    struct play_rec *rec;
    int c;

    rec = get_playback();
    if (rec != NULL) {
        c = Core.rec[rec->index++];
        if (c == (char) 0xff) {
            c = 0x100 | Core.rec[rec->index++];
        }
        return c;
    }

    c = getch();
    if (c == KEY_RESIZE) {
        update_screen_size();
        render_all();
        return -1;
    }

    if (c > 0xff) {
        /* does not fit into a single byte */
        if (Core.rec_len + 2 > Core.a_rec) {
            Core.a_rec *= 2;
            Core.a_rec += 2;
            Core.rec = xrealloc(Core.rec, Core.a_rec);
        }
        Core.rec[Core.rec_len++] = (char) 0xff;
        Core.rec[Core.rec_len++] = c & 0xff;
    } else {
        if (Core.rec_len + 1 > Core.a_rec) {
            Core.a_rec *= 2;
            Core.a_rec++;
            Core.rec = xrealloc(Core.rec, Core.a_rec);
        }
        Core.rec[Core.rec_len++] = c;
    }
    return c;
}

static int get_first_char(void)
{
    int c;
    size_t counter = 0, new_counter;

    Core.counter = 1;
    Core.user_reg = '.';
    while (c = get_ch(), (c == '0' && counter != 0) || (c >= '1' && c <= '9') ||
            (c == '\"')) {
        if (c == '\"') {
            if (counter != 0) {
                Core.counter = counter;
                counter = 0;
            }
            c = toupper(get_ch());
            if (!IS_REG_CHAR(c)) {
                return -1;
            }
            Core.user_reg = c;
        } else {
            /* add a digit to the counter */
            new_counter = counter * 10 + c - '0';
            /* check for overflow */
            if (new_counter < counter) {
                new_counter = SIZE_MAX;
            }
            counter = new_counter;
        }
    }

    if (counter == 0) {
        return c;
    }

    Core.counter = safe_mul(Core.counter, counter);
    return c;
}

int get_extra_char(void)
{
    int c;
    size_t counter = 0, new_counter;

    do {
        while (c = get_ch(), (c == '0' && counter != 0) ||
                             (c >= '1' && c <= '9')) {
            /* add a digit to the counter */
            new_counter = counter * 10 + c - '0';
            /* check for overflow */
            if (new_counter < counter) {
                new_counter = SIZE_MAX;
            }
            counter = new_counter;
        }
    } while (c == -1);

    if (counter == 0) {
        return c;
    }

    Core.counter = safe_mul(Core.counter, counter);
    return c;
}

int main(int argc, char **argv)
{
   static int (*const input_handlers[])(int c) = {
        [NORMAL_MODE] = normal_handle_input,
        [INSERT_MODE] = insert_handle_input,
        [VISUAL_MODE] = visual_handle_input,
        [VISUAL_BLOCK_MODE] = visual_handle_input,
        [VISUAL_LINE_MODE] = visual_handle_input,
    };

    int c;
    int r;
    int old_mode;
    size_t next_dot_i;
    struct play_rec *rec;
    struct undo_event *ev;

    if (init_purec(argc, argv) == -1) {
        return -1;
    }

    while (1) {
        render_all();

        do {
            rec = get_playback();
            next_dot_i = rec == NULL ? Core.rec_len : rec->index;
            if (Core.mode == INSERT_MODE) {
                c = get_ch();
            } else {
                Core.move_down_count = 0;
                do {
                    c = get_first_char();
                } while (c == -1);
            }

            old_mode = Core.mode;
            r = input_handlers[Core.mode](c);

            /* the dot recording is either the last key used in normal mode or
             * the key that led to a different mode all until the end of that
             * mode
             */
            if (old_mode == NORMAL_MODE) {
                if ((r & DO_RECORD)) {
                    Core.dot.from = next_dot_i;
                    Core.dot.to = rec == NULL ? Core.rec_len : rec->index;
                }
            } else {
                Core.dot.to = rec == NULL ? Core.rec_len : rec->index;
            }
        /* does not render the UI if nothing changed or when a recording is
         * playing which means: no active user input
         */
        } while ((r & UPDATE_UI) == 0 || get_playback() != NULL);

        if (Core.is_stopped) {
            break;
        }

        /* make all events at the end of the event list of a buffer non transient */
        for (struct buf *buf = FirstBuffer; buf != NULL; buf = buf->next) {
            if (buf->num_events == 0) {
                continue;
            }
            ev = &buf->events[buf->num_events - 1];
            ev->flags &= ~IS_TRANSIENT;
        }
    }

    return leave_purec();
}
