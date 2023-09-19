#include "purec.h"

int main(int argc, char **argv)
{
	edit_mode_t mode = EDIT_MODE_NORMAL;
	Edit *edit;

	setlocale(LC_ALL, "");

	initscr();
	noecho();
	raw();
	keypad(stdscr, true);
	refresh();

	edit = edit_new(buffer_load("test.txt"), 0, 0, COLS, LINES);
	while (1) {
		curs_set(0);
		mvprintw(13, 1, "want: i=%3zu, x=%2d, y=%2d",
			edit->wantindex, edit->wantcursor.x, edit->wantcursor.y);
		edit_update(edit);
		mvprintw(14, 1, "curs: i=%3zu, x=%2d, y=%2d",
			edit->buffer->gap.index, edit->cursor.x, edit->cursor.y);
		move(edit->cursor.y, edit->cursor.x);
		curs_set(1);
		const int c = getch();
		if (c == 0x03)
			break;
		edit_handle(edit, c, &mode);
	}

	curs_set(1);
	endwin();
	return 0;
}

