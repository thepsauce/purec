#include "purec.h"

Buffer first_buffer;

static void buffer_link(Buffer buf)
{
	if (first_buffer == NULL) {
		first_buffer = buf;
	} else {
		Buffer prev;

		for (prev = first_buffer;
			prev->next != NULL;
			prev = prev->next);
		prev->next = buf;
	}
}

Buffer buffer_new(void)
{
	Buffer buf;

	if ((buf = malloc(sizeof(*buf))) == NULL)
		return NULL;
	if ((buf->data = malloc(BUFFER_INITIAL_GAP_SIZE)) == NULL) {
		free(buf);
		return NULL;
	}
	buf->flags = 0;
	buf->file = NULL;
	buf->igap = 0;
	buf->ngap = BUFFER_INITIAL_GAP_SIZE;
	buf->n = 0;
	buffer_link(buf);
	return buf;
}

int buffer_destroy(Buffer *buf)
{
	for (size_t i = 0; i < n_buffers; i++) {
		if (all_buffers[i] == buf) {
			n_buffers--;
			memmove(all_buffers + i, all_buffers + i + 1,
				sizeof(*all_buffers) * (n_buffers - i));
			free(buf->path);
			free(buf->data);
			free(buf);
			return 0;
		}
	}
	return -1;
}


Buffer buffer_load(const char *path)
{
	Buffer buf;
	File file;
	char bom[4];
	char fc[1024];
	ssize_t r;

	for (buf = first_buffer; buf != NULL; buf = buf->next)
		if (buf->path && !strcmp(buf->path, path))
			return buf;
	if ((file = file_open(path)) == NULL)
		return NULL;
	if (!file_can_read(file))
		return NULL;
	if ((buf = malloc(sizeof(*buf))) == NULL) {
		file_close(file);
		return NULL;
	}
	if ((buf->data = file_read(file, &buf->n)) == NULL) {
		free(buf);
		file_close(file);
		return NULL;
	}
	buf->flags = 0;
	buf->file = file;
	buf->igap = buf->n;
	buf->ngap = 0;
	buffer_link(buf);
	return buf;
}

int buffer_read(Buffer *buf, const char *path);

int buffer_write(Buffer *buf, const char *path);

int buffer_setgap(Buffer *buf, size_t igap);

size_t buffer_puts(Buffer *buf, const char *str, size_t nstr);

size_t buffer_delete(Buffer *buf, int dir, size_t amount);
