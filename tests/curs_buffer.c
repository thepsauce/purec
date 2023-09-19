#include "test.h"

void buffer_dump(Buffer *buf, WINDOW *win)
{
	wmove(win, 0, 0);
	wprintw(win, "=== %s ===\n", buf->path);
	wprintw(win, "(%zu)[%zu](%zu)\n", buf->gap.index, buf->gap.size,
			buf->n - buf->gap.index);
	for (size_t i = 0;; i++) {
		if (i == buf->gap.index || i == buf->gap.index + buf->gap.size)
			waddch(win, '|' | COLOR_PAIR(5));
		if (i == buf->n + buf->gap.size)
			break;
		const unsigned char c = buf->data[i];
		if (c < 32) {
			wcolor_set(win, 4, NULL);
			wprintw(win, "^%c", c + '@');
		} else  {
			short pair;

			if (isalpha(c) || c == '_')
				pair = 2;
			else if (isdigit(c))
				pair = 3;
			else
				pair = 1;
			waddch(win, c | COLOR_PAIR(pair));
		}
	}
	wattrset(win, 0);
	wprintw(win, "\n=========\n");
}

int main(void)
{
	Buffer *buf;
	WINDOW *win;

	buf = buffer_load("test.txt");
	if (buf == NULL) {
		fprintf(stderr, "failed loading buffer\n");
		return -1;
	}

	setlocale(LC_ALL, "");

	initscr();
	start_color();
	noecho();
	raw();
	keypad(stdscr, true);
	refresh();
	init_pair(1, COLOR_WHITE, 0);
	init_pair(2, COLOR_GREEN, 0);
	init_pair(3, COLOR_MAGENTA, 0);
	init_pair(4, COLOR_BLUE, 0);
	init_pair(5, COLOR_RED, 0);

	win = newwin(LINES, COLS, 0, 0);
	scrollok(win, true);

	while (1) {
		buffer_dump(buf, win);
		wrefresh(win);
		const int c = getch();
		if (c == 0x03)
			break;
		switch (c) {
		case KEY_LEFT:
			if (buf->gap.index != 0)
				buffer_movegap(buf, buf->gap.index - 1);
			break;
		case KEY_RIGHT:
			if (buf->gap.index != buf->n)
				buffer_movegap(buf, buf->gap.index + 1);
			break;
		case KEY_DC:
			buffer_delete(buf, 1, 1);
			break;
		case KEY_BACKSPACE:
			buffer_delete(buf, -1, 1);
			break;
		default:
			if (isspace(c) || isgraph(c)) {
				buffer_puts(buf, &(char) { c }, 1);
			}
		}
	}

	endwin();
	buffer_destroy(buf);
	return 0;
}
