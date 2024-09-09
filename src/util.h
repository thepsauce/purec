#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdbool.h>

/**
 * Gets the number of elements of a static array.
 */
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))

/**
 * Gets the size of a literal string, usage:
 * size_t s1 = STRING_SIZE("String"),
 * s2 = STRING_SIZE("Bye");
 * And used no other way!
 */
#define STRING_SIZE(s) (sizeof(s)-1)

/**
 * Gets the maximum of two values, note that the argument may be executed twice.
 */
#define MAX(a, b) ((a)<(b)?(b):(a))

/**
 * Gets the minimum of two values, note that the argument may be executed twice.
 */
#define MIN(a, b) ((a)>(b)?(b):(a))

/**
 * Gets the control code of a letter.
 *
 * Example:
 * X becomes C-X.
 *
 * Note that the input must be a capital letter.
 */
#define CONTROL(k) ((k)-'A'+1)

/**
 * Position within "something".
 */
struct pos {
    size_t line;
    size_t col;
};

/**
 * Checks if the points are the same.
 *
 * @param p1    The first position.
 * @param p2    The second position.
 *
 * @return Whether the points are equal (`line` and `col` match).
 */
bool is_point_equal(const struct pos *p1, const struct pos *p2);

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
 * Multiplies two numbers without overflowing.
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
 * Adds two numbers without overflowing.
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
 * Gets the given path relative to the current directory (getcwd()).
 *
 * If the current path is "/home/gerhard//car/" and the input `path` is
 * "/home/gerhard/../blue/red" then this function returns "../../blue/red".
 * The resulting path is always a path that leads from the current working
 * directory to given `path`.
 *
 * @return The allocated string or `NULL` if `getcwd()` fails.
 */
char *get_relative_path(const char *path);

struct glyph {
    /// the wide char representation of this glyph
    wchar_t wc;
    /// number of bytes when this glyph is represented as multi byte string
    int n;
    /// the width this glyph occupies
    int w;
};

/**
 * Gets the first glyph of the multi byte string `s`.
 *
 * This function assumes that there is a "next glyph" and does not handle the
 * case where n==0 or, n==SIZE_MAX and s[0]=='\0'.
 *
 * @param s The multi byte string to extract the first glyph from.
 * @param n The length of the multi byte string, use `SIZE_MAX` for null
 *          terminated strings, this values must be greater than 0.
 * @param g The pointer to store the glyph data in.
 *
 * @return -1 if the multi byte sequence is invalid, otherwise the length of
 *         the multi byte sequence.
 */
int get_glyph(const char *s, size_t n, struct glyph *g);

#endif
