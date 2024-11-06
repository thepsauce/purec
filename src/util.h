#ifndef UTIL_H
#define UTIL_H

#include <limits.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

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

typedef long line_t;
typedef int col_t;

/* this is not needed anywhere so ovrewrite the name */
#undef LINE_MAX

#define LINE_MAX LONG_MAX
#define COL_MAX INT_MAX

#define PRLINE "%ld"
#define PRCOL "%d"

struct glyph {
    /// the wide char representation of this glyph
    wchar_t wc;
    /// number of bytes when this glyph is represented as multi byte string
    col_t n;
    /// the width this glyph occupies
    int w;
};

/**
 * Gets the number of bytes a utf8 character expects.
 *
 * @param ch    The utf8 byte to check.
 *
 * @return The number of bytes this utf8 leading byte expects.
 */
int get_expected_bytes(char ch);

/**
 * Get the number of bytes the glyph wants.
 *
 * @param s The multi byte sequence.
 * @param
 *
 * @return The number of bytes.
 */
int get_glyph_count(const char *s, size_t n);

#define move_forward_glyph(s, i, n) ((i)+get_glyph_count(&(s)[i], (n) - (i)))

/**
 * Moves given index to the previous glyph.
 *
 * @param s The multi byte sequence.
 * @param i The index after the glyph.
 *
 * @return The new index.
 */
size_t move_back_glyph(const char *s, size_t i);

#define tab_adjust(i, ts) ((ts)-(i)%(ts))

/**
 * Gets the index at given x position.
 *
 * @param s     The multi byte sequence.
 * @param n     The length of the multi byte sequence.
 * @param ts    The tab size to use.
 * @param w     The x position.
 *
 * @return The index at given x position.
 */
size_t get_index(const char *s, size_t n, int ts, size_t x);

/**
 * Gets the x position of given index.
 *
 * @param s     The multi byte sequence.
 * @param n     The length of the byte sequence.
 * @param ts    The tab size to use.
 * @param i     The index.
 *
 * @return The x position.
 */
size_t get_advance(const char *s, size_t n, int ts, size_t i);

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

/**
 * Position within "something".
 */
struct pos {
    line_t line;
    col_t col;
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
 * Gets the given path relative to the current working directory.
 *
 * If the path to start from is "/home/gerhard/car" and the input `path` is
 * "//home/gerhard/..//blue/red" then this function returns "../../blue/red".
 * The resulting path is always a path that leads from the current directory
 * to given `path`.
 *
 * @param path  The path to get the relative path from, must be sanitized.
 * @param from  The path to start from.
 *
 * @return The allocated string.
 */
char *get_relative_path(const char *path/*, const char *from*/);

/**
 * Gets the absolute path of given path.
 *
 * @param path  The path to get the absolute path from.
 * @param from  The path to start from if the `path` is relative.
 *
 * @return The allocated string.
 */
char *get_absolute_path(const char *path/*, const char *from*/);

/**
 * Gets characters until the first new line character.
 *
 * @param p_s   Pointer to a string.
 * @param p_a   Pointer to the capacity of the string.
 * @param fp    File to read from.
 *
 * @return Number of characters read or -1 if at end.
 */
ssize_t get_line(char **p_s, size_t *p_a, FILE *fp);

#define isidentf(c) (isalnum(c) || (c) == '_')

#endif
