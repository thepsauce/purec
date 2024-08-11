#include "buf.h"
#include "xalloc.h"

#include <string.h>

void free_event(struct undo_event *ev)
{
    for (size_t i = 0; i < ev->num_lines; i++) {
        free(ev->lines[i].s);
    }
    free(ev->lines);
    free(ev);
}

void get_end_pos(const struct undo_event *ev, struct pos *pos)
{
    pos->line = ev->pos.line + ev->num_lines - 1;
    pos->col = ev->lines[ev->num_lines - 1].n;
    if (pos->line == ev->pos.line) {
        pos->col += ev->pos.col;
    }
}

bool should_join(const struct undo_event *ev1, const struct undo_event *ev2)
{
    struct pos to1, to2;

    get_end_pos(ev1, &to1);
    get_end_pos(ev2, &to2);

    if (((ev1->flags | ev2->flags) & IS_REPLACE)) {
        return false;
    }
    if ((ev1->flags & IS_INSERTION)) {
        if ((ev2->flags & IS_INSERTION)) {
            return is_in_range(&ev2->pos, &ev1->pos, &to1);
        }
        return is_in_range(&ev2->pos, &ev1->pos, &to1) &&
            is_in_range(&to2, &ev1->pos, &to1);
    }
    if ((ev2->flags & IS_INSERTION)) {
        return is_point_equal(&ev1->pos, &ev2->pos);
    }
    return is_point_equal(&ev1->pos, &ev2->pos) ||
        is_point_equal(&ev1->pos, &to2);
}

struct undo_event *add_event(struct buf *buf, const struct undo_event *copy_ev)
{
    struct undo_event *ev;

    /* free old events */
    for (size_t i = buf->event_i; i < buf->num_events; i++) {
        free_event(buf->events[i]);
    }

    buf->events = xreallocarray(buf->events, buf->event_i + 1,
            sizeof(*buf->events));
    ev = xmalloc(sizeof(*ev));
    *ev = *copy_ev;
    ev->time = time(NULL);

    buf->events[buf->event_i++] = ev;
    buf->num_events = buf->event_i;
    return ev;
}

static void do_event(struct buf *buf, const struct undo_event *ev, int flags)
{
    struct line *line;
    struct pos to;

    if (flags & IS_REPLACE) {
        update_dirty_lines(buf, ev->pos.line, ev->pos.line + ev->num_lines - 1);
        line = &buf->lines[ev->pos.line];
        for (size_t i = 0; i < ev->lines[0].n; i++) {
            line->s[i + ev->pos.col] ^= ev->lines[0].s[i];
        }
        mark_dirty(line);
        for (size_t i = 1; i < ev->num_lines; i++) {
            line = &buf->lines[i + ev->pos.line];
            for (size_t j = 0; j < ev->lines[i].n; j++) {
                line->s[j] ^= ev->lines[i].s[j];
            }
            mark_dirty(line);
        }
    } else if (flags & IS_INSERTION) {
        _insert_lines(buf, &ev->pos, ev->lines, ev->num_lines);
    } else if (flags & IS_DELETION) {
        to.line = ev->pos.line + ev->num_lines - 1;
        to.col = ev->lines[ev->num_lines - 1].n;
        if (to.line == ev->pos.line) {
            to.col += ev->pos.col;
        }
        _delete_range(buf, &ev->pos, &to);
    }
}

struct undo_event *undo_event(struct buf *buf)
{
    struct undo_event *ev;

    if (buf->event_i == 0) {
        return NULL;
    }

    do {
        ev = buf->events[--buf->event_i];
        do_event(buf, ev, ev->flags ^ (IS_INSERTION | IS_DELETION));
    } while (buf->event_i > 0 &&
            (buf->events[buf->event_i - 1]->flags & IS_TRANSIENT));
    return ev;
}

struct undo_event *redo_event(struct buf *buf)
{
    struct undo_event *ev;
    struct undo_event *first_ev = NULL;

    if (buf->event_i == buf->num_events) {
        return NULL;
    }

    do {
        ev = buf->events[buf->event_i++];
        do_event(buf, ev, ev->flags);
        if (first_ev == NULL) {
            first_ev = ev;
        }
    } while ((ev->flags & IS_TRANSIENT) && buf->event_i != buf->num_events);
    return first_ev;
}

struct undo_event *perform_event(struct buf *buf, const struct undo_event *p_ev)
{
    struct pos to;
    struct undo_event ev;

    ev.pos = p_ev->pos;
    if ((p_ev->flags & IS_DELETION)) {
        if (p_ev->pos.line + p_ev->num_lines > buf->num_lines) {
            return NULL;
        }

        /* check if the deletion counts match up */
        if (p_ev->num_lines == 1) {
            if (p_ev->pos.col + p_ev->lines[0].n > buf->lines[p_ev->pos.line].n) {
                return NULL;
            }
        } else {
            if (p_ev->pos.col + p_ev->lines[0].n != buf->lines[p_ev->pos.line].n) {
                return NULL;
            }
            if (p_ev->lines[p_ev->num_lines - 1].n >
                    buf->lines[p_ev->pos.line + p_ev->num_lines - 1].n) {
                return NULL;
            }
            for (size_t i = 1; i < p_ev->num_lines - 1; i++) {
                if (p_ev->lines[i].n != buf->lines[i].n) {
                    return NULL;
                }
            }
        }

        /* get deleted text */
        get_end_pos(p_ev, &to);
        ev.flags = IS_DELETION;
        ev.lines = get_lines(buf, &p_ev->pos, &to, &ev.num_lines);
    } else {
        /* duplicate lines */
        ev.flags = IS_INSERTION;
        ev.num_lines = p_ev->num_lines;
        ev.lines = xreallocarray(NULL, ev.num_lines, sizeof(*ev.lines));
        for (size_t i = 0; i < ev.num_lines; i++) {
            init_raw_line(&ev.lines[i], p_ev->lines[i].s, p_ev->lines[i].n);
        }
    }
    do_event(buf, p_ev, p_ev->flags);
    return add_event(buf, &ev);
}
