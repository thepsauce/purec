#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdbool.h>

/**
 * Position within "something".
 */
struct pos {
    size_t line;
    size_t col;
};

/**
 * Sets `p1` to the position that comes first and `p2` to last.
 *
 * @param p1    The first position.
 * @param p2    The second position.
 */
void sort_positions(struct pos *p1, struct pos *p2);

/**
 * Sets `p1` to the upper left corner and `p2` to the lower right corner.
 *
 * @param p1    The first position.
 * @param p2    The second position.
 */
void sort_block_positions(struct pos *p1, struct pos *p2);

/**
 * Checks whether given position is within given range.
 *
 * @param pos   The position to check.
 * @param from  The starting position of the range.
 * @param to    The end position of the range.
 *
 * @return Whether the position is in given range.
 */
bool is_in_range(const struct pos *pos,
        const struct pos *from, const struct pos *to);

/**
 * Checks whether given position is within given block.
 *
 * @param pos   The position to check.
 * @param from  The upper left position of the block.
 * @param to    The lower right position of the block.
 *
 * @return Whether the position is in given block.
 */
bool is_in_block(const struct pos *pos,
        const struct pos *from, const struct pos *to);

/**
 * Multiply two numbers without overflowing.
 *
 * If an overflow would occur, `SIZE_MAX` is returned instead.
 *
 * @param a The first operand.
 * @param b The second operand.
 *
 * @return Clipped product of `a` and `b`.
 */
size_t safe_mul(size_t a, size_t b);

/**
 * Add two numbers without overflowing.
 *
 * If an overflow would occur, `SIZE_MAX` is returned instead.
 *
 * @param a The first operand.
 * @param b The second operand.
 *
 * @return Clipped sum of `a` and `b`.
 */
size_t safe_add(size_t a, size_t b);

/**
 * Get the number of elements of a static array.
 */
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))

/**
 * Get the size of a literal string, usage:
 * size_t s1 = STRING_SIZE("String"),
 * s2 = STRING_SIZE("Bye");
 * And used no other way!
 */
#define STRING_SIZE(s) (sizeof(s)-1)

/**
 * Get the maximum of two values, note that the argument may be executed twice.
 */
#define MAX(a, b) ((a)<(b)?(b):(a))

/**
 * Get the minimum of two values, note that the argument may be executed twice.
 */
#define MIN(a, b) ((a)>(b)?(b):(a))

/**
 * Get the control code of a letter.
 *
 * X becomes C-X.
 */
#define CONTROL(k) ((k)-'A'+1)

#endif

