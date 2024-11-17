#include "charnum.h"
#include "xalloc.h"

#include <stdlib.h>
#include <string.h>

struct charnum g_one = { 1, (char*) "1", 1 };

void clear_char_num(struct charnum *c)
{
    free(c->digits);
}

void str_to_char_num(const char *str, size_t len, struct charnum *c)
{
    if (str[0] == '+') {
        str++;
        len--;
        c->sign = 1;
    } else if (str[0] == '-') {
        str++;
        len--;
        c->sign = -1;
    } else {
        c->sign = 1;
    }
    c->num_digits = len;
    c->digits = xmalloc(len);
    memcpy(c->digits, str, len);
}

char *char_num_to_str(const struct charnum *c, size_t *p_n)
{
    char            *s;
    size_t          n;

    n = c->num_digits;
    if (c->sign == -1) {
        n++;
    }
    s = xmalloc(n);
    if (c->sign == -1) {
        s[0] = '-';
        memcpy(&s[1], c->digits, c->num_digits);
    } else {
        memcpy(s, c->digits, n);
    }
    *p_n = n;
    return s;
}

void num_to_char_num(size_t num, struct charnum *c)
{
    size_t          n;
    size_t          i;

    c->sign = 1;
    if (num == 0) {
        c->num_digits = 1;
        c->digits = xmalloc(1);
        c->digits[0] = '0';
        return;
    }
    c->num_digits = 0;
    for (n = num; n > 0; n /= 10) {
        c->num_digits++;
    }
    c->digits = xmalloc(c->num_digits);
    for (i = c->num_digits; i > 0; i--) {
        c->digits[i - 1] = '0' + num % 10;
        num /= 10;
    }
}

int u_compare_char_nums(const struct charnum *c1, const struct charnum *c2)
{
    size_t          i;
    int             cmp;

    if (c1->num_digits < c2->num_digits) {
        return -1;
    }
    if (c1->num_digits > c2->num_digits) {
        return 1;
    }
    for (i = 0; i < c1->num_digits; i--) {
        cmp = c1->digits[i] - c2->digits[i];
        if (cmp < 0) {
            return -1;
        }
        if (cmp > 0) {
            return 1;
        }
    }
    return 0;
}

static void u_add_char_nums(const struct charnum *c1, const struct charnum *c2,
                            struct charnum *c)
{
    int             sum, carry;
    size_t          i, j, n;
    char            tmp;

    c->sign = c1->sign;
    c->num_digits = MAX(c1->num_digits, c2->num_digits) + 1;
    c->digits = xmalloc(c->num_digits);
    n = 0;
    i = c1->num_digits;
    j = c2->num_digits;
    carry = 0;
    while (carry > 0 || i > 0 || j > 0) {
        sum = carry;
        if (i > 0) {
            sum += c1->digits[--i] - '0';
        }
        if (j > 0) {
            sum += c2->digits[--j] - '0';
        }
        c->digits[n++] = '0' + sum % 10;
        carry = sum / 10;
    }
    for (i = 0; i < n / 2; i++) {
        tmp = c->digits[i];
        c->digits[i] = c->digits[n - i - 1];
        c->digits[n - i - 1] = tmp;
    }
    c->num_digits = n;
}

static void u_sub_char_nums(const struct charnum *c1, const struct charnum *c2,
                            struct charnum *c)
{
    int                     cmp;
    const struct charnum    *tmp1;
    size_t                  n, i, j;
    int                     diff, borrow;
    char                    tmp2;

    cmp = u_compare_char_nums(c1, c2);
    if (cmp < 0) {
        tmp1 = c1;
        c1 = c2;
        c2 = tmp1;
        c->sign = -1;
    } else {
        c->sign = 1;
    }

    c->num_digits = c1->num_digits;
    c->digits = xmalloc(c->num_digits);
    n = 0;
    borrow = 0;
    i = c1->num_digits;
    j = c2->num_digits;

    while (borrow > 0 || i > 0 || j > 0) {
        diff = borrow;
        if (i > 0) {
            diff += c1->digits[--i] - '0';
        }
        if (j > 0) {
            diff -= c2->digits[--j] - '0';
        }
        if (diff < 0) {
            diff += 10;
            borrow = -1;
        } else {
            borrow = 0;
        }
        c->digits[n++] = '0' + diff;
    }

    while (n > 1 && c->digits[n - 1] == '0') {
        n--;
    }

    c->num_digits = n;
    for (i = 0; i < n / 2; i++) {
        tmp2 = c->digits[i];
        c->digits[i] = c->digits[n - i - 1];
        c->digits[n - i - 1] = tmp2;
    }
}

void add_char_nums(const struct charnum *c1, const struct charnum *c2,
                            struct charnum *c)
{
    if (c1->sign == c2->sign) {
        u_add_char_nums(c1, c2, c);
        return;
    }
    if (c1->sign == -1) {
        u_sub_char_nums(c2, c1, c);
        return;
    }
    u_sub_char_nums(c1, c2, c);
}

void subtract_char_nums(const struct charnum *c1, const struct charnum *c2,
                        struct charnum *c)
{
    if (c2->sign != c2->sign) {
        u_add_char_nums(c1, c2, c);
        return;
    }
    if (c1->sign == -1) {
        u_sub_char_nums(c2, c1, c);
        return;
    }
    u_sub_char_nums(c1, c2, c);
}
