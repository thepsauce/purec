#include "purec.h"

#define FWIN_UPDATE_CURSOR 0x01
#define FWIN_UPDATE_INDEX 0x02
#define FWIN_UPDATE_VCT 0x04

typedef struct point {
	int x, y;
} Point;

struct window {
	int flags;
	WINDOW *win;
	Buffer buffer;
	Point cursor;
	Point wantcursor;
	size_t wantindex;
	int scroll;
	int vct;
};

void draw_window(struct window *win)
{
	WINDOW *w;
	Point prev, p;
	Point max;
	int cw;
	int curscroll = 0;

	w = win->win;
	wmove(w, 0, 0);
	getbegyx(w, p.y, p.x);
	getmaxyx(w, max.y, max.x);
	max.x -= p.x;
	max.y -= p.y;
	p = (Point) { 0, 0 };
	if (win->wantindex >= win->buffer->gap.index)
		win->wantindex += win->buffer->gap.size;
	for (size_t i = 0, n = win->buffer->n;; i++, n--) {
		char ch;

		if (i == win->buffer->gap.index)
			i += win->buffer->gap.size;
		if ((win->flags & FWIN_UPDATE_INDEX) && i == win->wantindex) {
			win->cursor = p;
			if (i > win->buffer->gap.index)
				i -= win->buffer->gap.size;
			buffer_movegap(win->buffer, i);
			i += win->buffer->gap.size;
			win->flags ^= FWIN_UPDATE_INDEX;
		}
		if (n == 0)
			break;
		ch = win->buffer->data[i];
		cw = ch == '\t' ? TABSIZE - (p.x % TABSIZE) : 1;
		prev = p;
		if (ch == '\n' || p.x + cw > max.x) {
			wclrtoeol(w);
			p.x = 0;
			p.y++;
			if (p.y > max.y) {
				wscrl(w, -1);
				curscroll++;
			}
			wmove(w, p.y - curscroll, 0);
		}
		if (ch != '\n') {
			waddch(w, ch);
			p.x += cw;
		}
		if ((win->flags & FWIN_UPDATE_CURSOR) &&
				prev.y == win->wantcursor.y) {
			if (prev.y != p.y || p.x > win->wantcursor.x) {
				if (i > win->buffer->gap.index)
					i -= win->buffer->gap.size;
				buffer_movegap(win->buffer, i);
				i += win->buffer->gap.size;
				win->flags ^= FWIN_UPDATE_CURSOR;
				win->cursor = prev;
			}
		}
	}
	if ((win->flags & FWIN_UPDATE_CURSOR)) {
		win->cursor = p;
		buffer_movegap(win->buffer, win->buffer->n);
		win->flags ^= FWIN_UPDATE_CURSOR;
	}
	if ((win->flags & FWIN_UPDATE_VCT)) {
		win->vct = win->cursor.x;
		win->flags ^= FWIN_UPDATE_VCT;
	}
	wclrtobot(w);
	wrefresh(w);
}

int main(int argc, char **argv)
{
	enum {
		MODE_NORMAL, MODE_INSERT
	} mode = MODE_NORMAL;
	struct window w;

	setlocale(LC_ALL, "");

	initscr();
	noecho();
	raw();
	keypad(stdscr, true);
	refresh();

	memset(&w, 0, sizeof(w));
	w.win = newwin(LINES, COLS, 0, 0);
	scrollok(w.win, true);
	w.buffer = buffer_load("test.txt");
	w.wantindex = w.buffer->n;
	w.flags |= FWIN_UPDATE_INDEX | FWIN_UPDATE_VCT;
	while (1) {
		curs_set(0);
		mvprintw(13, 1, "want: i=%3zu, x=%2d, y=%2d", w.wantindex, w.wantcursor.x, w.wantcursor.y);
		draw_window(&w);
		mvprintw(14, 1, "curs: i=%3zu, x=%2d, y=%2d", w.buffer->gap.index, w.cursor.x, w.cursor.y);
		move(w.cursor.y, w.cursor.x);
		curs_set(1);
		const int c = getch();
		if (c == 0x03)
			break;
		w.wantcursor = w.cursor;
		w.wantindex = w.buffer->gap.index;
		if (mode == MODE_INSERT) {
		switch (c) {
		case 0x1b:
			mode = MODE_NORMAL;
			break;
		case KEY_LEFT:
			w.wantcursor.x--;
			w.flags |= FWIN_UPDATE_CURSOR | FWIN_UPDATE_VCT;
			break;
		case KEY_RIGHT:
			w.wantcursor.x++;
			w.flags |= FWIN_UPDATE_CURSOR | FWIN_UPDATE_VCT;
			break;
		case KEY_UP:
			if (w.wantcursor.y != 0) {
				w.wantcursor.y--;
				w.wantcursor.x = w.vct;
			}
			w.flags |= FWIN_UPDATE_CURSOR;
			break;
		case KEY_DOWN:
			w.wantcursor.y++;
			w.wantcursor.x = w.vct;
			w.flags |= FWIN_UPDATE_CURSOR;
			break;
		case KEY_DC:
			buffer_delete(w.buffer, 1, 1);
			break;
		case 0x7f: case KEY_BACKSPACE:
			if (buffer_delete(w.buffer, -1, 1) != 0) {
				w.wantindex = w.buffer->gap.index;
				w.flags |= FWIN_UPDATE_INDEX | FWIN_UPDATE_VCT;
			}
			break;
		default:
			if (!isgraph(c) && !isblank(c))
				break;
			buffer_puts(w.buffer, &(char) { c }, 1);
			w.wantindex = w.buffer->gap.index;
			w.flags |= FWIN_UPDATE_INDEX | FWIN_UPDATE_VCT;
		}
		} else if (mode == MODE_NORMAL) {
		switch (c) {
		case 'i':
			mode = MODE_INSERT;
			break;
		case 'h':
			w.wantcursor.x--;
			w.flags |= FWIN_UPDATE_CURSOR | FWIN_UPDATE_VCT;
			break;
		case 'l':
			w.wantcursor.x++;
			w.flags |= FWIN_UPDATE_CURSOR | FWIN_UPDATE_VCT;
			break;
		case 'k':
			if (w.wantcursor.y != 0) {
				w.wantcursor.y--;
				w.wantcursor.x = w.vct;
			}
			w.flags |= FWIN_UPDATE_CURSOR;
			break;
		case 'j':
			w.wantcursor.y++;
			w.wantcursor.x = w.vct;
			w.flags |= FWIN_UPDATE_CURSOR;
			break;
		case '0':
			w.wantcursor.x = 0;
			w.vct = 0;
			w.flags |= FWIN_UPDATE_CURSOR;
			break;
		case '$':
			w.wantcursor.x = INT_MAX;
			w.vct = INT_MAX;
			w.flags |= FWIN_UPDATE_CURSOR;
			break;
		case ' ':
			if (w.wantindex != w.buffer->n)
				w.wantindex++;
			w.flags |= FWIN_UPDATE_INDEX | FWIN_UPDATE_VCT;
			break;
		case 0x7f: case KEY_BACKSPACE:
			if (w.wantindex != 0)
				w.wantindex--;
			w.flags |= FWIN_UPDATE_INDEX | FWIN_UPDATE_VCT;
			break;
		}
		}
	}

	endwin();
	return 0;
}

