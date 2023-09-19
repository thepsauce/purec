#include "purec.h"

Window *first_window;

static void window_link(Window *win)
{
	win->next = first_window;
	first_window = win;
}

static void window_unlink(Window *win)
{
	if (win == first_window) {
		first_window = win->next;
	} else {
		Window *prev;

		for (prev = first_window;
				prev->next != win;
				prev = prev->next);
		prev->next = win->next;
	}
}

Window *window_new(size_t sz, int64_t flags, int x, int y, int w, int h)
{
	Window *win;

	win = malloc(sz);
	if (win == NULL)
		return NULL;
	memset(win, 0, sz);
	win->flags = flags;
	win->win = newwin(h, w, y, x);
	if (win->win == NULL) {
		free(win);
		return NULL;
	}
	scrollok(win->win, true);
	window_link(win);
	return win;
}
