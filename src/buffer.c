#include "purec.h"

Buffer *first_buffer;

static void buffer_link(Buffer *buf)
{
	buf->next = first_buffer;
	first_buffer = buf;
}

static void buffer_unlink(Buffer *buf)
{
	if (buf == first_buffer) {
		first_buffer = buf->next;
	} else {
		Buffer *prev;

		for (prev = first_buffer;
				prev->next != buf;
				prev = prev->next);
		prev->next = buf->next;
	}
}

Buffer *buffer_new(void)
{
	Buffer *buf;

	if ((buf = malloc(sizeof(*buf))) == NULL) {
		//purec_errno = PEMALLOC;
		return NULL;
	}
	if ((buf->data = malloc(BUFFER_GAP_SIZE)) == NULL) {
		free(buf);
		//purec_errno = PEMALLOC;
		return NULL;
	}
	buf->flags = 0;
	buf->path = NULL;
	buf->gap.index = 0;
	buf->gap.size = BUFFER_GAP_SIZE;
	buf->n = 0;
	buffer_link(buf);
	return buf;
}

void buffer_destroy(Buffer *buf)
{
	buffer_unlink(buf);
	free(buf->path);
	free(buf->data);
	free(buf);
}

Buffer *buffer_load(const char *path)
{
	Buffer *buf;
	struct stat st;

	for (buf = first_buffer; buf != NULL; buf = buf->next)
		if (buf->path && !strcmp(buf->path, path))
			return buf;
	if (stat(path, &st) < 0) {
		//purec_errno = PESTAT;
		return NULL;
	}
	if (!(st.st_mode & S_IRUSR)) {
		//purec_errno = PERACCESS;
		return NULL;
	}
	if ((buf = malloc(sizeof(*buf))) == NULL) {
		//purec_errno = PEMALLOC;
		return NULL;
	}
	if ((buf->path = strdup(path)) == NULL) {
		free(buf);
		//purec_errno = PEMALLOC;
		return NULL;
	}
	buf->st = st;
	if ((buf->data = read_file_utf8(path, &buf->n)) == NULL) {
		free(buf);
		/* read_file_uft8 sets the appropiate errno */
		return NULL;
	}
	buf->flags = 0;
	buf->gap.index = buf->n;
	buf->gap.size = 0;

	buffer_link(buf);
	return buf;
}

int buffer_read(Buffer *buf, const char *path)
{
	char *data;
	size_t ndata;

	if ((data = read_file_utf8(path, &ndata)) == NULL) {
		/* read_file_uft8 sets the appropiate errno */
		return -1;
	}
	buffer_puts(buf, data, ndata);
	return 0;
}

int buffer_write(Buffer *buf, const char *path)
{
	int fd;
	struct stat st;

	if (path == NULL && (path = buf->path) == NULL) {
		//purec_errno = PEPATH;
		return -1;
	}
	if (stat(path, &st) < 0) {
		//purec_errno = PESTAT;
		return -1;
	}
	if (!(st.st_mode & S_IWUSR)) {
		//purec_errno = PEWACCESS;
		return -1;
	}
	if (buf->path != NULL && !strcmp(path, buf->path)) {
		if (!memcmp(&st.st_ctim, &buf->st.st_ctim,
					sizeof(st.st_ctim))) {
			//purec_errno = PECOUTSIDE;
			return -1;
		}
		buf->st = st;
	}
	if ((fd = open(path, O_WRONLY)) < 0) {
		//purec_errno = PEOPEN;
		return -1;
	}
	write(fd, buf->data, buf->gap.index);
	write(fd, buf->data + buf->gap.index + buf->gap.size, buf->n - buf->gap.index);
	close(fd);
	if (buf->path == NULL && (buf->path = strdup(path)) == NULL) {
		//purec_errno = PEMALLOC;
		return -1;
	}
	return 0;
}

int buffer_movegap(Buffer *buf, size_t igap)
{
	igap = MIN(igap, buf->n);
	if (igap > buf->gap.index)
		memmove(buf->data + buf->gap.index,
			buf->data + buf->gap.index + buf->gap.size,
			igap - buf->gap.index);
	else
		memmove(buf->data + igap + buf->gap.size,
			buf->data + igap,
			buf->gap.index - igap);
	buf->gap.index = igap;
	return igap;
}

size_t buffer_puts(Buffer *buf, const char *str, size_t nstr)
{
	/* if gap is too small to insert n characters, increase gap size */
	if (nstr > buf->gap.size) {
		char *const newdata = realloc(buf->data, buf->n +
			nstr + BUFFER_GAP_SIZE);
		if (newdata == NULL) {
			//purec_errno = PEMALLOC;
			return 0;
		}
		buf->data = newdata;
		memmove(newdata + buf->gap.index + nstr + BUFFER_GAP_SIZE,
			newdata + buf->gap.index + buf->gap.size,
			buf->n - buf->gap.index);
		buf->gap.size = nstr + BUFFER_GAP_SIZE;
	}
	/* insert string into gap */
	memcpy(buf->data + buf->gap.index, str, nstr);
	buf->gap.index += nstr;
	buf->gap.size -= nstr;
	buf->n += nstr;
	//purec_errno = PESUCCESS;
	return nstr;
}

size_t buffer_delete(Buffer *buf, int dir, size_t amount)
{
	if (dir > 0) {
		amount = MIN(amount, buf->n - buf->gap.index);
		buf->gap.size += amount;
	} else {
		amount = MIN(amount, buf->gap.index);
		buf->gap.index -= amount;
		buf->gap.size += amount;
	}
	buf->n -= amount;
	return amount;
}
