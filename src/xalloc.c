#include "xalloc.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void *xmalloc(size_t size)
{
    void *ptr;

    if (size == 0) {
        return NULL;
    }
    ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "malloc(%zu): %s\n",
                size, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *xcalloc(size_t nmemb, size_t size)
{
    void *ptr;

    if (nmemb == 0 || size == 0) {
        return NULL;
    }
    ptr = calloc(nmemb, size);
    if (ptr == NULL) {
        fprintf(stderr, "calloc(%zu, %zu): %s\n",
                nmemb, size, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *xrealloc(void *ptr, size_t size)
{
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    ptr = realloc(ptr, size);
    if (ptr == NULL) {
        fprintf(stderr, "realloc(%p, %zu): %s\n",
                ptr, size, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *xreallocarray(void *ptr, size_t nmemb, size_t size)
{
    if (nmemb == 0 || size == 0) {
        free(ptr);
        return NULL;
    }
    ptr = reallocarray(ptr, nmemb, size);
    if (ptr == NULL) {
        fprintf(stderr, "reallocarray(%p, %zu, %zu): %s\n",
                ptr, nmemb, size, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *xmemdup(const void *ptr, size_t size)
{
    char *p_dup;

    if (size == 0) {
        return NULL;
    }

    p_dup = xmalloc(size);
    if (p_dup == NULL) {
        fprintf(stderr, "xmemdup(%p, %zu): %s\n",
                ptr, size, strerror(errno));
        exit(EXIT_FAILURE);
    }
    memcpy(p_dup, ptr, size);
    return p_dup;
}

void *xstrdup(const char *s)
{
    char *s_dup;

    s_dup = strdup(s);
    if (s_dup == NULL) {
        fprintf(stderr, "strdup(%s): %s\n",
                s, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return s_dup;
}

void *xstrndup(const char *s, size_t n)
{
    char *s_dup;

    s_dup = strndup(s, n);
    if (s_dup == NULL) {
        fprintf(stderr, "strndup(%.*s, %zu): %s\n",
                (int) n, s, n, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return s_dup;
}

char *xasprintf(const char *fmt, ...)
{
    va_list l;
    char *s;

    va_start(l, fmt);
    if (vasprintf(&s, fmt, l) == -1) {
        fprintf(stderr, "vasprintf(%s): %s\n",
                fmt, strerror(errno));
        exit(EXIT_FAILURE);
    }
    va_end(l);
    return s;
}
