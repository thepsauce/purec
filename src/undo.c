#include "buf.h"
#include "xalloc.h"

void free_event(struct undo_event *ev)
{
    for (size_t i = 0; i < ev->num_lines; i++) {
        free(ev->lines[i].s);
    }
    free(ev->lines);
    free(ev);
}

bool should_join(struct undo_event *ev1, struct undo_event *ev2)
{
    struct pos to1, to2;

    to1.line = ev1->pos.line + ev1->num_lines - 1;
    to1.col = ev1->lines[ev1->num_lines - 1].n;
    if (to1.line == ev1->pos.line) {
        to1.col += ev1->pos.col;
    }

    to2.line = ev2->pos.line + ev2->num_lines - 1;
    to2.col = ev2->lines[ev2->num_lines - 1].n;
    if (to2.line == ev2->pos.line) {
        to2.col += ev2->pos.col;
    }

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

static void do_event(struct buf *buf, struct undo_event *ev, int flags)
{
    struct line *line;
    struct pos to;

    if (flags & IS_REPLACE) {
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

next_event:
    ev = buf->events[--buf->event_i];
    do_event(buf, ev, ev->flags ^ (IS_INSERTION | IS_DELETION));

    if (buf->event_i > 0 &&
            (buf->events[buf->event_i - 1]->flags & IS_TRANSIENT)) {
        goto next_event;
    }
    return ev;
}

struct undo_event *redo_event(struct buf *buf)
{
    struct undo_event *ev;

    if (buf->event_i == buf->num_events) {
        return NULL;
    }

next_event:
    ev = buf->events[buf->event_i++];
    do_event(buf, ev, ev->flags);

    if ((ev->flags & IS_TRANSIENT) && buf->event_i != buf->num_events) {
        goto next_event;
    }
    return ev;
}

