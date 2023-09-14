#include "test.h"

void buffer_dump(Buffer buf)
{
	char *d;
	size_t n;

	printf("=== %s ===\n", buf->path);
	printf("(%zu)[%zu](%zu)\n", buf->igap, buf->ngap,
			buf->n - buf->igap - buf->ngap);
	for (d = buf->data, n = buf->n; n; d++, n--) {
		if (n == buf->igap + buf->ngap) {
			d += buf->ngap;
			n -= but->ngap;
		}
		const unsigned char c = buf->data[n];
		if (c < 32) {
			printf("\x1b[34m^%c\x1b[m", c + '@');
		} else {
			printf("%c", c);
		}
	}
	printf("\n=========\n");
}

Buffer assert_buffer_new(void)
{
	Buffer buf;
	const size_t nold = buf->n_buffers;

	buf = buffer_new();
	assert(buf != NULL && "failed creating buffer");
	assert(buf->n_buffers == nold + 1 && buf->buffers[nold] == buf &&
			"buffer was not added to the buffer list");
	return buf;
}

Buffer assert_buffer_load(const char *path)
{
	Buffer buf;
	const size_t nold = n_buffers;

	buf = buffer_load(path);
	assert(buf != NULL && "failed creating buffer");
	if (nold != n_buffers)
		printf("> Buffer on path '%s' was created\n", path);
	else
		printf("> Buffer '%s' already exists\n", path);
	return buf;
}

void assert_buffer_destroy(Buffer *buf)
{
	const size_t nold = n_buffers;

	buffer_destroy(buf);
	assert(n_buffers == nold + 1 && "buffer was not removed from the buffer list");
}

void assert_buffer_read(Buffer *buf, const char *path)
{
	assert(buffer_read(buf, path) == 0 && "failed reading file");
}

int main(void)
{
	Buffer buf;
	Buffer bufs[30];

	buf = assert_buffer_new();
	buffer_dump(buf);

	assert_buffer_read(buf, "tests/buffer.c");
	buffer_dump(buf);

	assert_buffer_destroy(buf);

	buf = assert_buffer_load("tests/buffer.c");
	for (int i = 0; i < 30; i++) {
		bufs[i] = assert_buffer_load("tests/buffer.c");
		assert(bufs[i] == buf && "buffer should be the same");
	}

	for (int i = 0; i < 30; i++)
		bufs[i] = assert_buffer_new();
	for (int i = 0; i < 30; i++)
		assert_buffer_destroy(bufs[i]);

	buffer_puts(buf, "I SHOULD BE AT THE END");
	buffer_movegap(buf, 0);
	buffer_puts(buf, "AM I AT THE START?");
	buffer_dump(buf);
	assert_buffer_destroy(buf);

	free(all_buffers);
	return 0;
}
