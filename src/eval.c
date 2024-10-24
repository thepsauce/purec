#include "purec.h"
#include "parse.h"
#include "xalloc.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int pass_single(struct value *v, struct value *values)
{
    copy_value(v, &values[0]);
    return 0;
}

static int operate(struct value *v, struct value *values, size_t n, int opr);

int bool_not(struct value *v, struct value *values)
{
    v->type = VALUE_BOOL;
    v->v.b = !values[0].v.b;
    return 0;
}

int bool_and_bool(struct value *v, struct value *values)
{
    v->type = VALUE_BOOL;
    v->v.b = values[0].v.b & values[1].v.b;
    return 0;
}

int bool_or_bool(struct value *v, struct value *values)
{
    v->type = VALUE_BOOL;
    v->v.b = values[0].v.b | values[1].v.b;
    return 0;
}

int bool_xor_bool(struct value *v, struct value *values)
{
    v->type = VALUE_BOOL;
    v->v.b = values[0].v.b ^ values[1].v.b;
    return 0;
}

int number_less_number(struct value *v, struct value *values)
{
    v->type = VALUE_BOOL;
    v->v.b = values[0].v.f < values[1].v.f;
    return 0;
}

int number_less_equal_number(struct value *v, struct value *values)
{
    v->type = VALUE_BOOL;
    v->v.b = values[0].v.f <= values[1].v.f;
    return 0;
}

int number_greater_number(struct value *v, struct value *values)
{
    v->type = VALUE_BOOL;
    v->v.b = values[0].v.f > values[1].v.f;
    return 0;
}

int number_greater_equal_number(struct value *v, struct value *values)
{
    v->type = VALUE_BOOL;
    v->v.b = values[0].v.f > values[1].v.f;
    return 0;
}

int number_equal_number(struct value *v, struct value *values)
{
    v->type = VALUE_BOOL;
    v->v.b = values[0].v.f == values[1].v.f;
    return 0;
}

int number_not_equal_number(struct value *v, struct value *values)
{
    v->type = VALUE_BOOL;
    v->v.b = values[0].v.f != values[1].v.f;
    return 0;
}

int number_negate(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = -values[0].v.f;
    return 0;
}

int number_plus_number(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = values[0].v.f + values[1].v.f;
    return 0;
}

int number_minus_number(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = values[0].v.f - values[1].v.f;
    return 0;
}

int number_multiply_number(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = values[0].v.f * values[1].v.f;
    return 0;
}

int number_divide_number(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = values[0].v.f / values[1].v.f;
    return 0;
}

int string_plus_string(struct value *v, struct value *values)
{
    v->type = VALUE_STRING;
    v->v.s.n = values[0].v.s.n + values[1].v.s.n;
    v->v.s.p = xmalloc(v->v.s.n);
    memcpy(v->v.s.p, values[0].v.s.p, values[0].v.s.n);
    memcpy(&v->v.s.p[values[0].v.s.n], values[1].v.s.p, values[1].v.s.n);
    return 0;
}

int compute_tolower(struct value *v, struct value *values)
{
    size_t          i;

    v->type = VALUE_STRING;
    v->v.s.n = values[0].v.s.n;
    v->v.s.p = xmalloc(v->v.s.n);
    for (i = 0; i < v->v.s.n; i++) {
        v->v.s.p[i] = tolower(values[0].v.s.p[i]);
    }
    return 0;
}

int compute_toupper(struct value *v, struct value *values)
{
    size_t          i;

    v->type = VALUE_STRING;
    v->v.s.n = values[0].v.s.n;
    v->v.s.p = xmalloc(v->v.s.n);
    for (i = 0; i < v->v.s.n; i++) {
        v->v.s.p[i] = toupper(values[0].v.s.p[i]);
    }
    return 0;
}

int compute_substr1(struct value *v, struct value *values)
{
    size_t          from;

    if (values[1].type != VALUE_NUMBER) {
        set_error("substr() expects a number as 2nd operand");
        return -1;
    }
    v->type = VALUE_STRING;
    from = roundl(values[1].v.f);
    if (from >= values[0].v.s.n) {
        v->v.s.n = 0;
        v->v.s.p = NULL;
        return 0;
    }
    v->v.s.n = values[0].v.s.n - from;
    v->v.s.p = xmemdup(&values[0].v.s.p[from], v->v.s.n);
    return 0;
}

int compute_substr2(struct value *v, struct value *values)
{
    size_t          from, to;

    if (values[1].type != VALUE_NUMBER || values[2].type != VALUE_NUMBER) {
        set_error("substr() expects a number as 2nd and 3rd argument");
        return -1;
    }
    v->type = VALUE_STRING;
    from = roundl(values[1].v.f);
    to = roundl(values[2].v.f);
    to = MIN(values[0].v.s.n, to);
    if (from >= to) {
        v->v.s.n = 0;
        v->v.s.p = NULL;
        return 0;
    }
    v->v.s.n = to - from;
    v->v.s.p = xmemdup(&values[0].v.s.p[from], v->v.s.n);
    return 0;
}

int compute_len(struct value *v, struct value *values)
{
    if (values[0].type != VALUE_STRING) {
        set_error("len() expects a string as first argument");
        return -1;
    }
    v->type = VALUE_NUMBER;
    v->v.f = values[0].v.s.n;
    return 0;
}

int compute_num(struct value *v, struct value *values)
{
    char            *end;

    v->type = VALUE_NUMBER;
    return parse_number(values[0].v.s.p, values[0].v.s.n, &end, &v->v.f);
}

int compute_sin(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = sinl(values[0].v.f);
    return 0;
}

int compute_cos(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = cosl(values[0].v.f);
    return 0;
}

int compute_tan(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = tanl(values[0].v.f);
    return 0;
}

int compute_asin(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = asinl(values[0].v.f);
    return 0;
}

int compute_acos(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = acosl(values[0].v.f);
    return 0;
}

int compute_atan(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = atanl(values[0].v.f);
    return 0;
}

int compute_sinh(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = sinhl(values[0].v.f);
    return 0;
}

int compute_cosh(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = coshl(values[0].v.f);
    return 0;
}

int compute_tanh(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = tanhl(values[0].v.f);
    return 0;
}

int compute_asinh(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = asinhl(values[0].v.f);
    return 0;
}

int compute_acosh(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = acoshl(values[0].v.f);
    return 0;
}

int compute_atanh(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = atanhl(values[0].v.f);
    return 0;
}

int compute_mod(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = fmodl(values[0].v.f, values[1].v.f);
    return 0;
}

int compute_exp(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = expl(values[0].v.f);
    return 0;
}

int compute_ln(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = logl(values[0].v.f);
    return 0;
}

int compute_sqrt(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = sqrtl(values[0].v.f);
    return 0;
}

int compute_cbrt(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = cbrtl(values[0].v.f);
    return 0;
}

int compute_root(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = powl(values[0].v.f, 1 / values[1].v.f);
    return 0;
}

int compute_max(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = fmaxl(values[0].v.f, values[0].v.f);
    return 0;
}

int compute_min(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = fminl(values[0].v.f, values[0].v.f);
    return 0;
}

int compute_floor(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = floorl(values[0].v.f);
    return 0;
}

int compute_ceil(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = ceill(values[0].v.f);
    return 0;
}

int compute_erf(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = erfl(values[0].v.f);
    return 0;
}

int compute_erfc(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = erfcl(values[0].v.f);
    return 0;
}

int compute_pow(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = powl(values[0].v.f, values[1].v.f);
    return 0;
}

int compute_tgamma(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = tgammal(values[0].v.f);
    return 0;
}

int compute_trunc(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = truncl(values[0].v.f);
    return 0;
}

int compute_round(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = roundl(values[0].v.f);
    return 0;
}

int compute_raise2(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = values[0].v.f * values[0].v.f;
    return 0;
}

int compute_raise3(struct value *v, struct value *values)
{
    v->type = VALUE_NUMBER;
    v->v.f = values[0].v.f * values[0].v.f * values[0].v.f;
    return 0;
}

static int operate(struct value *v, struct value *values, size_t n, int opr)
{
    static int (*single_operators[GROUP_MAX][VALUE_MAX])(struct value *v, struct value *values) = {
        [GROUP_POSITIVE][VALUE_NUMBER] = pass_single,

        [GROUP_NEGATE][VALUE_NUMBER] = number_negate,
        [GROUP_NOT][VALUE_NUMBER] = bool_not,

        [GROUP_ROUND][VALUE_BOOL] = pass_single,
        [GROUP_ROUND][VALUE_NUMBER] = pass_single,
        [GROUP_ROUND][VALUE_STRING] = pass_single,

        [GROUP_SQRT][VALUE_NUMBER] = compute_sqrt,
        [GROUP_CBRT][VALUE_NUMBER] = compute_cbrt,

        [GROUP_RAISE2][VALUE_NUMBER] = compute_raise2,
        [GROUP_RAISE3][VALUE_NUMBER] = compute_raise3,
    };

    static int (*double_operators[GROUP_MAX][VALUE_MAX][VALUE_MAX])(struct value *v, struct value *values) = {
        [GROUP_PLUS][VALUE_NUMBER][VALUE_NUMBER] = number_plus_number,
        [GROUP_MINUS][VALUE_NUMBER][VALUE_NUMBER] = number_minus_number,
        [GROUP_MULTIPLY][VALUE_NUMBER][VALUE_NUMBER] = number_multiply_number,
        [GROUP_DIVIDE][VALUE_NUMBER][VALUE_NUMBER] = number_divide_number,
        [GROUP_MOD][VALUE_NUMBER][VALUE_NUMBER] = compute_mod,
        [GROUP_RAISE][VALUE_NUMBER][VALUE_NUMBER] = compute_pow,

        [GROUP_PLUS][VALUE_STRING][VALUE_STRING] = string_plus_string,
        [GROUP_MULTIPLY][VALUE_STRING][VALUE_STRING] = string_plus_string,

        [GROUP_AND][VALUE_BOOL][VALUE_BOOL] = bool_and_bool,
        [GROUP_OR][VALUE_BOOL][VALUE_BOOL] = bool_or_bool,
        [GROUP_XOR][VALUE_BOOL][VALUE_BOOL] = bool_xor_bool,

        [GROUP_LESS][VALUE_NUMBER][VALUE_NUMBER] = number_less_number,
        [GROUP_LESS_EQUAL][VALUE_NUMBER][VALUE_NUMBER] = number_less_equal_number,
        [GROUP_GREATER][VALUE_NUMBER][VALUE_NUMBER] = number_greater_number,
        [GROUP_GREATER_EQUAL][VALUE_NUMBER][VALUE_NUMBER] = number_greater_equal_number,
        [GROUP_EQUAL][VALUE_NUMBER][VALUE_NUMBER] = number_equal_number,
        [GROUP_NOT_EQUAL][VALUE_NUMBER][VALUE_NUMBER] = number_not_equal_number,
    };

    int (*op)(struct value *v, struct value *vs);

    switch (n) {
    case 1: op = single_operators[opr][values[0].type]; break;
    case 2: op = double_operators[opr][values[0].type][values[1].type]; break;
    default:
        op = NULL;
    }

    if (op == NULL) {
        set_error("operator not defined");
        return -1;
    }
    return (*op)(v, values);
}

struct {
    const char *name;
    size_t num_args;
    int (*opr)(struct value *v, struct value *values);
} system_functions[] = {
    { "sin", 1, compute_sin },
    { "cos", 1, compute_cos },
    { "tan", 1, compute_tan },
    { "asin", 1, compute_asin },
    { "acos", 1, compute_acos },
    { "atan", 1, compute_atan },
    { "sinh", 1, compute_sinh },
    { "cosh", 1, compute_cosh },
    { "tanh", 1, compute_tanh },
    { "asinh", 1, compute_asinh },
    { "acosh", 1, compute_acosh },
    { "atanh", 1, compute_atanh },
    { "mod", 2, compute_mod },
    { "exp", 1, compute_exp },
    { "ln", 1, compute_ln },
    { "sqrt", 1, compute_sqrt },
    { "cbrt", 1, compute_cbrt },
    { "root", 2, compute_root },
    { "max", 2, compute_max },
    { "min", 2, compute_min },
    { "ceil", 1, compute_ceil },
    { "floor", 1, compute_floor },
    { "erf", 1, compute_erf },
    { "erfc", 1, compute_erfc },
    { "pow", 2, compute_pow },
    { "tgamma", 1, compute_tgamma },
    { "trunc", 1, compute_trunc },
    { "round", 1, compute_round },
    { "num", 1, compute_num },
    { "tolower", 1, compute_tolower },
    { "toupper", 1, compute_toupper },
    { "substr", 2, compute_substr1 },
    { "substr", 3, compute_substr2 },
    { "len", 1, compute_len },
};

bool has_system_function(const char *name)
{
    unsigned        i;

    for (i = 0; i < ARRAY_SIZE(system_functions); i++) {
        if (strcmp(system_functions[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

int compute_system_function(const char *name, struct value *values,
                            size_t num_values, struct value *v)
{
    unsigned        i;

    for (i = 0; i < ARRAY_SIZE(system_functions); i++) {
        if (strcmp(system_functions[i].name, name) == 0 &&
                system_functions[i].num_args == num_values) {
            break;
        }
    }
    if (i == ARRAY_SIZE(system_functions)) {
        return 1;
    }
    return system_functions[i].opr(v, values);
}

void copy_value(struct value *dest, const struct value *src)
{
    dest->type = src->type;
    switch (src->type) {
    case VALUE_BOOL:
        dest->v.b = src->v.b;
        break;

    case VALUE_NUMBER:
        dest->v.f = src->v.f;
        break;

    case VALUE_STRING:
        dest->v.s.p = xmemdup(src->v.s.p, src->v.s.n);
        dest->v.s.n = src->v.s.n;
        break;
    }
}

void clear_value(struct value *v)
{
    switch (v->type) {
    case VALUE_STRING:
        free(v->v.s.p);
        break;
    }
}

void clear_values(struct value *values, size_t n)
{
    while (n > 0) {
        n--;
        clear_value(&values[n]);
    }
}

static int compute_deep_value(struct group *g, struct value *v);

static int compute_function_call(const char *name, size_t var_i,
                                 struct group *arg, struct value *v)
{
    struct value *args, a;
    size_t num_args;
    size_t i;
    size_t item_i;
    int err;
    size_t num_locals;

    args = NULL;
    num_args = 0;
    err = 0;
    while (arg->type == GROUP_COMMA) {
        args = xreallocarray(args, num_args + 1, sizeof(*args));
        err = compute_deep_value(arg->right, &args[num_args]);
        if (err != 0) {
            break;
        }
        num_args++;
        arg = arg->left;
    }
    if (err != 0) {
        clear_values(args, num_args);
        free(args);
        return -1;
    }
    args = xreallocarray(args, num_args + 1, sizeof(*args));
    err = compute_deep_value(arg, &args[num_args]);
    if (err != 0) {
        clear_values(args, num_args);
        free(args);
        return -1;
    }
    num_args++;

    /* reverse arguments */
    for (i = 0; i < num_args / 2; i++) {
        a = args[i];
        args[i] = args[num_args - 1 - i];
        args[num_args - 1 - i] = a;
    }

    if (var_i == SIZE_MAX) {
        err = compute_system_function(name, args, num_args, v);
        if (err == 1) {
            set_error("invalid number of parameters of function '%s' does not exist", name);
        }
    } else {
        for (item_i = 0; item_i < Parser.vars[var_i].num_items; item_i++) {
            if (Parser.vars[var_i].items[item_i].num_args == num_args) {
                break;
            }
        }
        if (item_i == Parser.vars[var_i].num_items) {
            err = compute_system_function(name, args, num_args, v);
            if (err == 1) {
                set_error("invalid number of parameters for '%s'", name);
                clear_values(args, num_args);
                free(args);
                err = -1;
            }
            return err;
        }
        num_locals = Parser.num_locals;
        for (i = 0; i < num_args; i++) {
            push_local_variable(Parser.vars[var_i].items[item_i].args[i],
                                &args[i]);
        }
        err = compute_deep_value(&Parser.vars[var_i].items[item_i].group, v);
        /* pop the local variables */
        Parser.num_locals = num_locals;
    }
    clear_values(args, num_args);
    free(args);
    return err;
}

static int compute_variable(struct group *group, struct value *value)
{
    size_t          var_i;
    struct var_set  *var;

    var_i = get_local_variable(group->v.w);
    if (var_i != SIZE_MAX) {
        copy_value(value, &Parser.locals[var_i].value);
        return 0;
    }
    var_i = get_variable(group->v.w);
    if (var_i == SIZE_MAX) {
        set_error("variable '%s' does not exist", group->v.w);
        return -1;
    }
    var = &Parser.vars[var_i];
    /* TODO: add variable value type */
    if (var->items[0].num_args > 0) {
        set_error("variable '%s' does expect at least %zu arguments",
                  group->v.w, var->items[0].num_args);
        return -1;
    }
    return compute_deep_value(&var->items[0].group, value);
}

static int compute_implicit(struct group *left, struct group *right,
                            struct value *value)
{
    size_t          var_i;
    struct value    values[2];
    int             err;
    struct group    *arg;

    if (left->type == GROUP_VARIABLE) {
        var_i = get_variable(left->v.w);
        /* in these cases we intepret it as function call:
         * a(...)
         * a ... where a is a variable accepting at least one argument
         * a(...), a ... where a is a system function
         */
        if ((var_i != SIZE_MAX || has_system_function(left->v.w)) &&
                (right->type == GROUP_ROUND || var_i == SIZE_MAX ||
                 Parser.vars[var_i].items[0].num_args > 0)) {
            arg = right->type == GROUP_ROUND ? right->left : right;
            return compute_function_call(left->v.w, var_i, arg, value);
        }
    }
    if (compute_deep_value(left, &values[0]) == -1) {
        return -1;
    }
    if (compute_deep_value(right, &values[1]) == -1) {
        clear_value(&values[0]);
        return -1;
    }
    err = operate(value, values, 2, GROUP_MULTIPLY);
    clear_value(&values[0]);
    clear_value(&values[1]);
    return err;
}

static int compute_deep_value(struct group *group, struct value *value)
{
    struct value    values[2];
    int             err;

    switch (group->type) {
    case GROUP_NUMBER:
        value->type = VALUE_NUMBER;
        value->v.f = group->v.f;
        return 0;

    case GROUP_STRING:
        value->type = VALUE_STRING;
        value->v.s.p = xmemdup(group->v.s.p, group->v.s.n);
        value->v.s.n = group->v.s.n;
        return 0;

    case GROUP_VARIABLE:
        return compute_variable(group, value);

    case GROUP_IMPLICIT:
        return compute_implicit(group->left, group->right, value);
    }

    if (group->left == NULL && group->right == NULL) {
        set_error("not sure what's wrong");
        return -1;
    }

    if (compute_deep_value(group->left, &values[0]) == -1) {
        return -1;
    }
    if (group->right == NULL) {
        err = operate(value, values, 1, group->type);
    } else {
        if (compute_deep_value(group->right, &values[1]) == -1) {
            clear_value(&values[0]);
            return -1;
        }
        err = operate(value, values, 2, group->type);
        clear_value(&values[1]);
    }
    clear_value(&values[0]);
    return err;
}

static inline int compute_implicit_declare(struct group *left,
                                           struct group *right,
                                           struct value *value)
{
    char            **args, *d;
    size_t          num_args;
    struct group    *var, *impl, *c;
    size_t          i;

    var = left->left;
    if (var->type != GROUP_VARIABLE) {
        return 1;
    }

    impl = left->right;
    switch (impl->type) {
    case GROUP_VARIABLE:
        /* function depending on a single variable */
        (void) add_variable(var->v.w, right, &impl->v.w, 1);
        value->type = VALUE_NULL;
        break;

    case GROUP_ROUND:
        /* function depending on any amount of variables */
        c = impl->left;
        args = NULL;
        num_args = 0;
        while (c->type == GROUP_COMMA) {
            if (c->right->type != GROUP_VARIABLE) {
                free(args);
                return 1;
            }
            args = xreallocarray(args, num_args + 1, sizeof(*args));
            args[num_args++] = c->right->v.w;
            c = c->left;
        }
        if (c->type != GROUP_VARIABLE) {
            free(args);
            return 1;
        }
        args = xreallocarray(args, num_args + 1, sizeof(*args));
        args[num_args++] = c->v.w;
        /* reverse */
        for (i = 0; i < num_args / 2; i++) {
            d = args[i];
            args[i] = args[num_args - 1 - i];
            args[num_args - 1 - i] = d;
        }
        (void) add_variable(var->v.w, right, args, num_args);
        value->type = VALUE_NULL;
        free(args);
        break;

    default:
        return 1;
    }
    return 0;
}

static int compute_shallow_equal(struct group *left, struct group *right,
                                 struct value *value)
{
    size_t          var_i;

    if (left->type == GROUP_VARIABLE) {
        var_i = get_variable(left->v.w);
        if (var_i == SIZE_MAX || Parser.vars[var_i].items[0].num_args > 0) {
            (void) add_variable(left->v.w, right, NULL, 0);
        } else {
            /* variable already exists, overwrite the value */
            clear_group(&Parser.vars[var_i].items[0].group);
            copy_group(&Parser.vars[var_i].items[0].group, right);
        }
        value->type = VALUE_NULL;
        return 0;
    }
    
    if (left->type == GROUP_IMPLICIT) {
        return compute_implicit_declare(left, right, value);
    }
    return 1;
}

int compute_value(struct group *group, struct value *value)
{
    int             err;

    if (group->type == GROUP_EQUAL) {
        err = compute_shallow_equal(group->left, group->right, value);
        if (err <= 0) {
            return err;
        }
    }
    return compute_deep_value(group, value);
}
