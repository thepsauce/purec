#ifndef CHARNUM_H
#define CHARNUM_H

#include "util.h"

/**
 * A character number is stored as series of characters '0' to '9'.
 * The number zero has the form `{ 1, "0", 1 }`, there is no 0 sign.
 */
struct charnum {
    int sign;
    char *digits;
    size_t num_digits;
};

/**
 * The number 1 as char number.
 */
extern struct charnum g_one;

/**
 * Convert a string that consists only of a leading sign and digits to a char
 * number.
 *
 * @param str   The string to convert.
 * @param len   The length of the string to convert.
 * @param c     The result of the conversion.
 */
void str_to_char_num(const char *str, size_t len, struct charnum *c);

/**
 * Convert a char number to a string.
 *
 * @param c     The char number to convert.
 * @param p_n   The length of the allocated string.
 *
 * @return The allocated string.
 */
char *char_num_to_str(const struct charnum *c, size_t *p_n);

/**
 * Convert an integer to a char number.
 *
 * @param num   The number to convert from.
 * @param c     The result of the conversion.
 */
void num_to_char_num(size_t num, struct charnum *c);

/**
 * Free resources associated to given charnum.
 *
 * The char number to free resources from.
 */
void clear_char_num(struct charnum *c);

/**
 * Compare two char numbers while ignoring their sign.
 *
 * @param c1    The first char number.
 * @param c2    The second char number.
 * @return -1 if |c1| < |c2|, 0 if |c1| == |c2|, 1 otherwise.
 */
int u_compare_char_nums(const struct charnum *c1, const struct charnum *c2);

/**
 * Add two char numbers together.
 *
 * @param c1    The first char number.
 * @param c2    The second char number.
 */
void add_char_nums(const struct charnum *c1, const struct charnum *c2,
                   struct charnum *c);

/**
 * Subtract one char number from another.
 *
 * @param c1    The first char number.
 * @param c2    The second char number.
 */
void subtract_char_nums(const struct charnum *c1, const struct charnum *c2,
                        struct charnum *c);

#endif
