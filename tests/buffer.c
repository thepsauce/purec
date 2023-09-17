#include "test.h"

void buffer_dump(Buffer buf)
{
	printf("=== %s ===\n", buf->path);
	printf("(%zu)[%zu](%zu)\n", buf->igap, buf->ngap,
			buf->n - buf->igap);
	for (size_t i = 0;; i++) {
		if (i == buf->igap || i == buf->igap + buf->ngap)
			printf("\x1b[31m|\x1b[m");
		if (i == buf->n + buf->ngap)
			break;
		const unsigned char c = buf->data[i];
		if (c < 32)
			printf("\x1b[34m^%c\x1b[m", c + '@');
		else if (isalpha(c) || c == '_')
			printf("\x1b[32m%c\x1b[m", c);
		else if (isdigit(c))
			printf("\x1b[36m%c\x1b[m", c);
		else
			printf("\x1b[35m%c\x1b[m", c);
	}
	printf("\n=========\n");
}

Buffer assert_buffer_new(void)
{
	Buffer buf;

	buf = buffer_new();
	assert(buf != NULL && "failed creating buffer");
	return buf;
}

Buffer assert_buffer_load(const char *path)
{
	Buffer buf;

	buf = buffer_load(path);
	assert(buf != NULL && "failed creating buffer");
	return buf;
}

void assert_buffer_destroy(Buffer buf)
{
	buffer_destroy(buf);
}

void assert_buffer_read(Buffer buf, const char *path)
{
	assert(buffer_read(buf, path) == 0 && "failed reading file");
}

int main(void)
{
	Buffer buf;
	Buffer bufs[30];
	const char *s1 = "I SHOULD BE AT THE END",
	      *s2 = "AM I AT THE START?";

	buf = assert_buffer_new();
	buffer_dump(buf);

	assert_buffer_read(buf, "tests/buffer.c");
	buffer_dump(buf);

	assert_buffer_destroy(buf);

	buf = assert_buffer_load("tests/buffer.c");
	for (int i = 0; i < (int) ARRLEN(bufs); i++) {
		bufs[i] = assert_buffer_load("tests/buffer.c");
		assert(bufs[i] == buf && "buffer should be the same");
	}

	for (int i = 0; i < (int) ARRLEN(bufs); i++)
		bufs[i] = assert_buffer_new();
	for (int i = 0; i < (int) ARRLEN(bufs); i++)
		assert_buffer_destroy(bufs[i]);

	buffer_puts(buf, s1, strlen(s1));
	buffer_movegap(buf, 0);
	buffer_puts(buf, s2, strlen(s2));
	buffer_delete(buf, 1, 118);
 	buffer_dump(buf);
	assert_buffer_destroy(buf);
	return 0;
}
