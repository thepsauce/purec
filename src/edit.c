#include "purec.h"

Edit *edit_new(Buffer *buf, int x, int y, int w, int h)
{
	Edit *edit;
	void *handle;
	Syntax *syn;

	if (buf == NULL)
		return NULL;

	edit = (Edit*) window_new(sizeof(*edit),
		FWIN_UPDATE_INDEX | FWIN_UPDATE_VCT,
		x, y, w, h);
	if (edit == NULL)
		return NULL;
	edit->buffer = buf;
	handle = dlopen("build/c.so", RTLD_LAZY);
	if (handle != NULL) {
		Syntax *(*const snew)(void) = dlsym(handle, "syntax_new");
		syn = snew();
		if (syn != NULL) {
			syn->buffer = buf;
			syn->next = dlsym(handle, "syntax_next");
			syn->destroy = dlsym(handle, "syntax_destroy");
			edit->syntax = syn;
		}
	}
	return edit;
}

void edit_update(Edit *edit)
{
	Buffer *const buf = edit->buffer;
	Syntax *const syn = edit->syntax;
	WINDOW *w;
	Point prev, p;
	Point max;
	size_t ind;
	int cw;
	int curscroll = 0;

	buffer_movegap(buf, buf->n);

	w = edit->win.win;
	wmove(w, 0, 0);
	getbegyx(w, p.y, p.x);
	getmaxyx(w, max.y, max.x);
	max.x -= p.x;
	max.y -= p.y;
	p = (Point) { 0, 0 };
	memset((void*) &syn->index, 0, (void*) syn - (void*) &syn->index);
	while (ind = syn->index, syn->index != buf->n) {
		while (syn->next(syn) == 1);
		for (; ind != syn->index; ind++) {
			if ((edit->win.flags & FWIN_UPDATE_INDEX) &&
					ind == edit->wantindex) {
				edit->cursor = p;
				edit->index = ind;
				edit->win.flags ^= FWIN_UPDATE_INDEX;
			}
			const char ch = buf->data[ind];
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
				waddch(w, ch | syn->attr);
				p.x += cw;
			}
			if ((edit->win.flags & FWIN_UPDATE_CURSOR) &&
					prev.y == edit->wantcursor.y) {
				if (prev.y != p.y || p.x > edit->wantcursor.x) {
					edit->index = ind;
					edit->win.flags ^= FWIN_UPDATE_CURSOR;
					edit->cursor = prev;
				}
			}
		}
	}
	if ((edit->win.flags & FWIN_UPDATE_CURSOR)) {
		edit->cursor = p;
		edit->index = buf->n;
		edit->win.flags ^= FWIN_UPDATE_CURSOR;
	}
	if ((edit->win.flags & FWIN_UPDATE_VCT)) {
		edit->vct = edit->cursor.x;
		edit->win.flags ^= FWIN_UPDATE_VCT;
	}
	wclrtobot(w);
	wrefresh(w);
}

void edit_movecursor(Edit *edit, Point newcursor, bool updatevct)
{
	if (edit->cursor.x == newcursor.x &&
			edit->cursor.y == newcursor.y)
		return;
	edit->wantcursor = newcursor;
	edit->win.flags |= FWIN_UPDATE_CURSOR;
	if (updatevct)
		edit->win.flags |= FWIN_UPDATE_VCT;
}

static void edit_normal(Edit *edit, int c, edit_mode_t *mode)
{
	edit->wantindex = edit->index;
	edit->wantcursor = edit->cursor;
	switch (c) {
	case 'i':
		*mode = EDIT_MODE_INSERT;
		break;
	case 'h':
		edit->wantcursor.x--;
		edit->win.flags |= FWIN_UPDATE_CURSOR | FWIN_UPDATE_VCT;
		break;
	case 'l':
		edit->wantcursor.x++;
		edit->win.flags |= FWIN_UPDATE_CURSOR | FWIN_UPDATE_VCT;
		break;
	case 'k':
		if (edit->wantcursor.y != 0) {
			edit->wantcursor.y--;
			edit->wantcursor.x = edit->vct;
		}
		edit->win.flags |= FWIN_UPDATE_CURSOR;
		break;
	case 'j':
		edit->wantcursor.y++;
		edit->wantcursor.x = edit->vct;
		edit->win.flags |= FWIN_UPDATE_CURSOR;
		break;
	case '0':
		edit->wantcursor.x = 0;
		edit->vct = 0;
		edit->win.flags |= FWIN_UPDATE_CURSOR;
		break;
	case '$':
		edit->wantcursor.x = INT_MAX;
		edit->vct = INT_MAX;
		edit->win.flags |= FWIN_UPDATE_CURSOR;
		break;
	case ' ':
		if (edit->wantindex != edit->buffer->n)
			edit->wantindex++;
		edit->win.flags |= FWIN_UPDATE_INDEX | FWIN_UPDATE_VCT;
		break;
	case 0x7f: case KEY_BACKSPACE:
		if (edit->wantindex != 0)
			edit->wantindex--;
		edit->win.flags |= FWIN_UPDATE_INDEX | FWIN_UPDATE_VCT;
		break;
	}
}


static void edit_insert(Edit *edit, int c, edit_mode_t *mode)
{
	edit->wantindex = edit->index;
	edit->wantcursor = edit->cursor;
	switch (c) {
	case 0x1b:
		*mode = EDIT_MODE_NORMAL;
		break;
	case KEY_LEFT:
		edit->wantcursor.x--;
		edit->win.flags |= FWIN_UPDATE_CURSOR | FWIN_UPDATE_VCT;
		break;
	case KEY_RIGHT:
		edit->wantcursor.x++;
		edit->win.flags |= FWIN_UPDATE_CURSOR | FWIN_UPDATE_VCT;
		break;
	case KEY_UP:
		if (edit->wantcursor.y != 0) {
			edit->wantcursor.y--;
			edit->wantcursor.x = edit->vct;
		}
		edit->win.flags |= FWIN_UPDATE_CURSOR;
		break;
	case KEY_DOWN:
		edit->wantcursor.y++;
		edit->wantcursor.x = edit->vct;
		edit->win.flags |= FWIN_UPDATE_CURSOR;
		break;
	case KEY_DC:
		buffer_movegap(edit->buffer, edit->index);
		buffer_delete(edit->buffer, 1, 1);
		break;
	case 0x7f: case KEY_BACKSPACE:
		buffer_movegap(edit->buffer, edit->index);
		if (buffer_delete(edit->buffer, -1, 1) != 0) {
			edit->wantindex = edit->buffer->gap.index;
			edit->win.flags |= FWIN_UPDATE_INDEX | FWIN_UPDATE_VCT;
		}
		break;
	default:
		if (!isgraph(c) && !isspace(c))
			break;
		buffer_movegap(edit->buffer, edit->index);
		buffer_puts(edit->buffer, &(char) { c }, 1);
		edit->wantindex = edit->buffer->gap.index;
		edit->win.flags |= FWIN_UPDATE_INDEX | FWIN_UPDATE_VCT;
	}
}

void edit_handle(Edit *edit, int c, edit_mode_t *mode)
{
	switch (*mode) {
	case EDIT_MODE_NORMAL:
		edit_normal(edit, c, mode);
		break;
	case EDIT_MODE_INSERT:
		edit_insert(edit, c, mode);
		break;
	}
}

