#include "buf.h"
#include "frame.h"
#include "xalloc.h"

#include <string.h>

struct undo Undo;

bool should_join(const struct undo_event *ev1, const struct undo_event *ev2)
{
    if (((ev1->flags | ev2->flags) & (IS_REPLACE | IS_BLOCK))) {
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
    char *data, *new_s;
    size_t data_len;
    time_t cur_time;
    struct tm *tm;

    data = lines[0].s;
    data_len = lines[0].n;

    for (size_t i = 1; i < num_lines; i++) {
        data_len += 1 + lines[i].n;
    }

    data = xrealloc(data, data_len);
    lines[0].s = data;
    for (size_t i = 1, j = lines[0].n; i < num_lines; i++) {
        data[j++] = '\n';

        new_s = memcpy(&data[j], lines[i].s, lines[i].n);
        j += lines[i].n;

        free(lines[i].s);

        lines[i].s = new_s;
    }

    l = 0;
    r = Undo.num_segments;
    while (l < r) {
        m = (l + r) / 2;

        seg = &Undo.segments[m];

        if (num_lines < seg->num_lines) {
            cmp = -1;
            goto diff;
        }
        if (num_lines > seg->num_lines) {
            cmp = 1;
            goto diff;
        }

        for (size_t i = 0; i < num_lines; i++) {
            if (lines[i].n < seg->lines[i].n) {
                cmp = -1;
                goto diff;
            }
            if (lines[i].n > seg->lines[i].n) {
                cmp = 1;
                goto diff;
            }
        }

        (void) load_undo_data(m);

        cmp = memcmp(data, seg->data, data_len);

        unload_undo_data(seg);

        if (cmp != 0) {
            goto diff;
        }

        /* do not need these anymore, already stored this information */
        free(data);
        free(lines);
        return m;

    diff:
        if (cmp < 0) {
            l = m + 1;
        } else {
            r = m;
        }
    }

    if (Undo.num_segments + 1 > Undo.a_segments) {
        Undo.a_segments *= 2;
        Undo.a_segments++;
        Undo.segments = xreallocarray(Undo.segments, Undo.a_segments,
                sizeof(*Undo.segments));
    }
    seg = &Undo.segments[Undo.num_segments];
    if (data_len > HUGE_UNDO_THRESHOLD) {
        if (Undo.fp == NULL) {
            cur_time = time(NULL);
            tm = localtime(&cur_time);

            new_s = xasprintf("/tmp/undo_data_%04d-%02d-%02d_%02d-%02d-%02d",
                    Core.cache_dir,
                    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                    tm->tm_hour, tm->tm_min, tm->tm_sec);
            Undo.fp = fopen(new_s, "w+");
            free(new_s);
        }
        fseek(Undo.fp, 0, SEEK_END);
        fgetpos(Undo.fp, &seg->file_pos);
        fwrite(data, 1, data_len, Undo.fp);
        for (size_t i = 0; i < num_lines; i++) {
            lines[i].s -= (size_t) data;
        }
        free(data);
        data = NULL;
    }
    seg->data = data;
    seg->data_len = data_len;
    seg->lines = lines;
    seg->num_lines = num_lines;
    seg->load_count = 0;
    return Undo.num_segments++;
}

struct undo_seg *load_undo_data(size_t data_i)
{
    struct undo_seg *seg;

    if (data_i >= Undo.num_segments) {
        return NULL;
    }

    seg = &Undo.segments[data_i];
    seg->load_count++;
    if (seg->data == NULL) {
        seg->data = xmalloc(seg->data_len);

        for (size_t i = 0; i < seg->num_lines; i++) {
            seg->lines[i].s += (size_t) seg->data;
        }

        fsetpos(Undo.fp, &seg->file_pos);
        fread(seg->data, 1, seg->data_len, Undo.fp);
    }
    return seg;
}

void unload_undo_data(struct undo_seg *seg)
{
    seg->load_count--;
    if (seg->data_len > HUGE_UNDO_THRESHOLD && seg->load_count == 0) {
        for (size_t i = 0; i < seg->num_lines; i++) {
            seg->lines[i].s -= (size_t) seg->data;
        }
        free(seg->data);
        seg->data = NULL;
    }
}

struct undo_event *add_event(struct buf *buf, int flags, const struct pos *pos,
        struct raw_line *lines, size_t num_lines)
{
    struct undo_event *ev;
    size_t max_n;
    struct mark *mark;

    /* free old events */
    for (size_t i = buf->event_i; i < buf->num_events; i++) {
        free(buf->events[i]);
    }

    buf->events = xreallocarray(buf->events, buf->event_i + 1,
            sizeof(*buf->events));
    ev = xmalloc(sizeof(*ev));
    ev->flags = flags;
    ev->pos = *pos;
    if ((flags & IS_BLOCK)) {
        ev->end.line = ev->pos.line + num_lines - 1;
        max_n = 0;
        for (size_t i = 0; i < num_lines; i++) {
            max_n = MAX(max_n, lines[i].n);
        }
        ev->end.col = ev->pos.col + max_n - 1;
    } else {
        ev->end.line = ev->pos.line + num_lines - 1;
        ev->end.col = lines[num_lines - 1].n;
        if (ev->end.line == ev->pos.line) {
            ev->end.col += ev->pos.col;
        }
    }
    ev->data_i = save_lines(lines, num_lines);
    ev->time = time(NULL);

    mark = &Core.marks['.' - MARK_MIN];
    mark->buf = buf;
    mark->pos = SelFrame->cur;

    buf->events[buf->event_i++] = ev;
    buf->num_events = buf->event_i;
    return ev;
}

static void do_event(struct buf *buf, const struct undo_event *ev, int flags)
{
    struct undo_seg *seg;
    struct line *line;

    if ((flags & IS_DELETION)) {
        if ((flags & IS_BLOCK)) {
            _delete_block(buf, &ev->pos, &ev->end);
        } else {
            _delete_range(buf, &ev->pos, &ev->end);
        }
        return;
    }

    seg = load_undo_data(ev->data_i);
    if ((flags & IS_REPLACE)) {
        update_dirty_lines(buf, ev->pos.line,
                ev->pos.line + seg->num_lines - 1);
        line = &buf->lines[ev->pos.line];
        for (size_t i = 0; i < seg->lines[0].n; i++) {
            line->s[i + ev->pos.col] ^= seg->lines[0].s[i];
        }
        mark_dirty(line);
        for (size_t i = 1; i < seg->num_lines; i++) {
            line = &buf->lines[i + ev->pos.line];
            for (size_t j = 0; j < seg->lines[i].n; j++) {
                line->s[j] ^= seg->lines[i].s[j];
            }
            mark_dirty(line);
        }
    } else {
        if ((flags & IS_BLOCK)) {
            _insert_block(buf, &ev->pos, seg->lines, seg->num_lines);
        } else {
            _insert_lines(buf, &ev->pos, seg->lines, seg->num_lines);
        }
    }
    unload_undo_data(seg);
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
