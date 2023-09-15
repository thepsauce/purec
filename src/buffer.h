#ifndef INCLUDED_BUFFER_H
#define INCLUDED_BUFFER_H

/**
 * The buffer is a simple data structure,
 * it uses a gap to speed up insertions and deletions.
 */

#define BUFFER_GAP_SIZE 64

typedef struct buffer {
	int flags;
	struct stat st;
	char *path;
	char *data;
	size_t igap, ngap;
	size_t n;
	struct buffer *next;
} *Buffer;

extern Buffer first_buffer;

/**
 * Create a new empty buffer and add it to the buffer list.
 */
Buffer buffer_new(void);

/**
 * Delete a buffer from the buffer list.
 */
void buffer_destroy(Buffer buf);

/**
 * Either creates a new buffer and returns that or returns a buffer
 * from the buffer list with the same path.
 *
 * NULL is returned if the path is invalid or if there is not enough memory.
 */
Buffer buffer_load(const char *path);

/**
 * Reads all content from the file at given path.
 */
int buffer_read(Buffer buf, const char *path);

/**
 * Writes the given buffer to given path. If the buffer's path is null,
 * it stores the path inside the buffer.
 *
 * If path is null, either the buffer writes to it's stored path or an
 * error is returned.
 */
int buffer_write(Buffer buf, const char *path);

/**
 * Sets new values for igap.
 */
int buffer_movegap(Buffer buf, size_t igap);

/**
 * Puts given string of length nStr into the buffer at the current gap.
 *
 * The number of characters inserted is returned.
 */
size_t buffer_puts(Buffer buf, const char *str, size_t nstr);

/**
 * Deletes content either left (dir = -1) or right (dir = 1) of the gap.
 *
 * The number of characters deleted is returned.
 */
size_t buffer_delete(Buffer buf, int dir, size_t amount);

#endif
