#include "buf.h"
#include "xalloc.h"

#include <string.h>

struct undo Undo;

bool should_join(const struct undo_event *ev1, const struct undo_event *ev2)
{
    if (((ev1->flags | ev2->flags) & IS_REPLACE)) {
        return false;
    }
    if ((ev1->flags & IS_INSERTION)) {
        if ((ev2->flags & IS_INSERTION)) {
            return is_in_range(&ev2->pos, &ev1->pos, &ev1->end);
        }
        return is_in_range(&ev2->pos, &ev1->pos, &ev1->end) &&
            is_in_range(&ev2->end, &ev1->pos, &ev1->end);
    }
    if ((ev2->flags & IS_INSERTION)) {
        return is_point_equal(&ev1->pos, &ev2->pos);
    }
    return is_point_equal(&ev1->pos, &ev2->pos) ||
        is_point_equal(&ev1->pos, &ev2->end);
}

size_t save_lines(struct raw_line *lines, size_t num_lines)
{
    size_t l, m, r;
    struct undo_seg *seg;
    int cmp;
    struct raw_line *seg_lines;
    size_t seg_num_lines;

    l = 0;
    r = Undo.num_segments;
    while (l < r) {
        m = (l + r) / 2;

        seg = &Undo.segments[m];

        seg_lines = NULL;

        if (num_lines < seg->num_lines) {
            cmp = -1;
            goto diff;
        } if (num_lines > seg->num_lines) {
            cmp = 1;
            goto diff;
        }

        seg_lines = load_undo_data(m, &seg_num_lines);

        for (size_t i = 0; i < num_lines; i++) {
            if (lines[i].n > seg_lines[i].n) {
                cmp = -1;
                goto diff;
            }
            if (lines[i].n < seg_lines[i].n) {
                cmp = 1;
                goto diff;
            }
        }

        for (size_t i = 0; i < num_lines; i++) {
            cmp = memcmp(lines[i].s, seg_lines[i].s, seg_lines[i].n);
            if (cmp != 0) {
                goto diff;
            }
        }

        for (size_t i = 0; i < num_lines; i++) {
            free(lines[i].s);
        }
        free(lines);
        unload_undo_data(m);
        return m;

    diff:
        if (cmp < 0) {
            l = m + 1;
        } else {
            r = m;
        }
        if (seg_lines != NULL) {
            unload_undo_data(m);
        }
    }

    if (Undo.num_segments + 1 > Undo.a_segments) {
        Undo.a_segments *= 2;
        Undo.a_segments++;
        Undo.segments = xreallocarray(Undo.segments, Undo.a_segments,
                sizeof(*Undo.segments));
    }
    seg = &Undo.segments[Undo.num_segments];
    if (num_lines > HUGE_UNDO_THRESHOLD) {
        if (Undo.fp == NULL) {
            Undo.fp = fopen("undo_data", "w+");
        }
        fseek(Undo.fp, 0, SEEK_END);
        fgetpos(Undo.fp, &seg->file_pos);
        fwrite(&num_lines, sizeof(num_lines), 1, Undo.fp);
        for (size_t i = 0; i < num_lines; i++) {
            fwrite(&lines[i].n, sizeof(lines[i].n), 1, Undo.fp);
        }
        for (size_t i = 0; i < num_lines; i++) {
            fwrite(lines[i].s, 1, lines[i].n, Undo.fp);
            free(lines[i].s);
        }
        free(lines);
        lines = NULL;
    }
    seg->lines = lines;
    seg->num_lines = num_lines;
    return Undo.num_segments++;
}

struct raw_line *load_undo_data(size_t data_i, size_t *p_num_lines)
{
    struct undo_seg *seg;

    seg = &Undo.segments[data_i];
    if (seg->lines == NULL) {
        seg->lines = xreallocarray(NULL, seg->num_lines,
                sizeof(*seg->lines));
        fsetpos(Undo.fp, &seg->file_pos);
        fseek(Undo.fp, sizeof(seg->num_lines), SEEK_CUR);
        for (size_t i = 0; i < seg->num_lines; i++) {
            fread(&seg->lines[i].n, sizeof(seg->lines[i].n), 1, Undo.fp);
        }
        for (size_t i = 0; i < seg->num_lines; i++) {
            seg->lines[i].s = xmalloc(seg->lines[i].n);
            fread(seg->lines[i].s, 1, seg->lines[i].n, Undo.fp);
        }
    }
    *p_num_lines = seg->num_lines;
    return seg->lines;
}

void unload_undo_data(size_t data_i)
{
    struct undo_seg *seg;

    seg = &Undo.segments[data_i];
    if (seg->num_lines > HUGE_UNDO_THRESHOLD) {
        for (size_t i = 0; i < seg->num_lines; i++) {
            free(seg->lines[i].s);
        }
        free(seg->lines);
        seg->lines = NULL;
    }
}

struct undo_event *add_event(struct buf *buf, int flags, const struct pos *pos,
        struct raw_line *lines, size_t num_lines)
{
    struct undo_event *ev;

    /* free old events */
    for (size_t i = buf->event_i; i < buf->num_events; i++) {
        free(buf->events[i]);
    }

    buf->events = xreallocarray(buf->events, buf->event_i + 1,
            sizeof(*buf->events));
    ev = xmalloc(sizeof(*ev));
    ev->flags = flags;
    ev->pos = *pos;
    ev->end.line = ev->pos.line + num_lines - 1;
    ev->end.col = lines[num_lines - 1].n;
    if (ev->end.line == ev->pos.line) {
        ev->end.col += ev->pos.col;
    }
    ev->data_i = save_lines(lines, num_lines);
    ev->time = time(NULL);

    buf->events[buf->event_i++] = ev;
    buf->num_events = buf->event_i;
    return ev;
}

static void do_event(struct buf *buf, const struct undo_event *ev, int flags)
{
    struct raw_line *lines;
    size_t num_lines;
    struct line *line;

    if ((flags & IS_DELETION)) {
        _delete_range(buf, &ev->pos, &ev->end);
        return;
    }

    lines = load_undo_data(ev->data_i, &num_lines);
    if ((flags & IS_REPLACE)) {
        update_dirty_lines(buf, ev->pos.line, ev->pos.line + num_lines - 1);
        line = &buf->lines[ev->pos.line];
        for (size_t i = 0; i < lines[0].n; i++) {
            line->s[i + ev->pos.col] ^= lines[0].s[i];
        }
        mark_dirty(line);
        for (size_t i = 1; i < num_lines; i++) {
            line = &buf->lines[i + ev->pos.line];
            for (size_t j = 0; j < lines[i].n; j++) {
                line->s[j] ^= lines[i].s[j];
            }
            mark_dirty(line);
        }
    } else {
        _insert_lines(buf, &ev->pos, lines, num_lines);
    }
    unload_undo_data(ev->data_i);
}

struct undo_event *undo_event(struct buf *buf)
{
    struct undo_event *ev;
    int flags;

    if (buf->event_i == 0) {
        return NULL;
    }

    do {
        ev = buf->events[--buf->event_i];
        /* reverse the insertion/deletion flags to undo */
        flags = ev->flags;
        if ((flags & (IS_INSERTION | IS_DELETION))) {
            flags ^= (IS_INSERTION | IS_DELETION);
        }
        do_event(buf, ev, flags);
    } while (buf->event_i > 0 &&
            (buf->events[buf->event_i - 1]->flags & IS_TRANSIENT));
    return ev;
}

struct undo_event *redo_event(struct buf *buf)
{
    struct undo_event *ev;

    if (buf->event_i == buf->num_events) {
        return NULL;
    }

    do {
        ev = buf->events[buf->event_i++];
        do_event(buf, ev, ev->flags);
    } while ((ev->flags & IS_TRANSIENT) && buf->event_i != buf->num_events);
    return ev;
}
