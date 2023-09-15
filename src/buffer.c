#include "purec.h"

Buffer first_buffer;

static void buffer_link(Buffer buf)
{
	buf->next = first_buffer;
	first_buffer = buf;
}

static void buffer_unlink(Buffer buf)
{
	if (buf == first_buffer) {
		first_buffer = buf->next;
	} else {
		Buffer prev;

		for (prev = first_buffer;
				prev->next != buf;
				prev = prev->next);
		prev->next = buf->next;
	}
}

Buffer buffer_new(void)
{
	Buffer buf;

	if ((buf = malloc(sizeof(*buf))) == NULL)
		return NULL;
	if ((buf->data = malloc(BUFFER_GAP_SIZE)) == NULL) {
		free(buf);
		return NULL;
	}
	buf->flags = 0;
	buf->path = NULL;
	buf->igap = 0;
	buf->ngap = BUFFER_GAP_SIZE;
	buf->n = 0;
	buffer_link(buf);
	return buf;
}

void buffer_destroy(Buffer buf)
{
	buffer_unlink(buf);
	free(buf->path);
	free(buf->data);
	free(buf);
}

Buffer buffer_load(const char *path)
{
	Buffer buf;
	struct stat st;
	int fd;
	iconv_t cd = (iconv_t) -1;
	char outbufst[1024];
	char *outbuf;
	size_t outbytesleft;
	char inbufst[1024];
	char *inbuf;
	size_t inbytesleft;
	char *newd;
	size_t nbytesconv;
	size_t nbytes;
	ssize_t r;

	for (buf = first_buffer; buf != NULL; buf = buf->next)
		if (buf->path && !strcmp(buf->path, path))
			return buf;
	if (stat(path, &st) < 0)
		return NULL;
	if (!(st.st_mode & S_IRUSR))
		return NULL;
	if ((buf = malloc(sizeof(*buf))) == NULL)
		return NULL;
	if ((buf->path = strdup(path)) == NULL) {
		free(buf);
		return NULL;
	}
	magic_t cookie = magic_open(MAGIC_MIME_ENCODING);
	magic_load(cookie, NULL);

	const char *encoding = magic_file(cookie, path);

	if (encoding != NULL)
		cd = iconv_open("UTF-8", encoding);

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		if (cd != (iconv_t) -1)
			iconv_close(cd);
		free(buf->path);
		free(buf);
		return NULL;
	}
	buf->data = NULL;
	buf->n = 0;
	inbytesleft = 0;
	while ((r = read(fd, inbufst + inbytesleft,
				sizeof(inbufst) - inbytesleft)) > 0) {
		inbuf = inbufst;
		inbytesleft += r;
		outbuf = outbufst;
		outbytesleft = sizeof(outbufst);
		nbytesconv = iconv(cd,
				&inbuf, &inbytesleft,
				&outbuf, &outbytesleft);
		if(nbytesconv == (size_t) -1)
			break;
		nbytes = sizeof(outbufst) - outbytesleft;
		newd = realloc(buf->data, buf->n + nbytes);
		if (newd == NULL)
			break;
		buf->data = newd;
		newd += buf->n;
		buf->n += nbytes;
		memcpy(newd, outbufst, nbytes);
		memmove(inbufst, inbuf, inbytesleft);
	}
	iconv_close(cd);
	magic_close(cookie);

	buf->flags = 0;
	buf->igap = buf->n;
	buf->ngap = 0;

	buffer_link(buf);
	return buf;
}

int buffer_read(Buffer buf, const char *path);

int buffer_write(Buffer buf, const char *path);

int buffer_movegap(Buffer buf, size_t igap)
{
	igap = MIN(igap, buf->n);
	if (igap > buf->igap)
		memmove(buf->data + buf->igap,
			buf->data + buf->igap + buf->ngap,
			igap - buf->igap);
	else
		memmove(buf->data + igap + buf->ngap,
			buf->data + igap,
			buf->igap - igap);
	buf->igap = igap;
	return igap;
}

size_t buffer_puts(Buffer buf, const char *str, size_t nstr)
{
	/* if gap is too small to insert n characters, increase gap size */
	if (nstr > buf->ngap) {
		char *const newdata = realloc(buf->data, buf->n +
			nstr + BUFFER_GAP_SIZE);
		if (newdata == NULL)
			return 0;
		memmove(newdata + buf->n + BUFFER_GAP_SIZE,
			newdata + buf->igap + buf->ngap,
			buf->n - buf->igap);
		buf->ngap = nstr + BUFFER_GAP_SIZE;
		buf->data = newdata;
	}
	/* insert string into gap */
	memcpy(buf->data + buf->igap, str, nstr);
	buf->igap += nstr;
	buf->ngap -= nstr;
	buf->n += nstr;
	return nstr;
}

size_t buffer_delete(Buffer buf, int dir, size_t amount)
{
	if (dir > 0) {
		amount = MIN(amount, buf->n - buf->igap);
		buf->ngap += amount;
	} else {
		amount = MIN(amount, buf->igap);
		buf->igap -= amount;
		buf->ngap += amount;
	}
	buf->n -= amount;
	return amount;
}
