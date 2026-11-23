/* Memory, growable vectors, string buffers and small file helpers. */
#ifndef BACKBURN_CORE_UTIL_H
#define BACKBURN_CORE_UTIL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *xrealloc(void *p, size_t n);
void *xcalloc(size_t n, size_t s);
char *xstrndup(const char *s, size_t n);
char *xstrdup(const char *s);

/* A growable array: VEC(T) v = {0}; PUSH(v, x); v.a[i], v.n. */
#define VEC(T) struct { T *a; size_t n, cap; }
#define PUSH(v, ...)                                                             \
    do {                                                                         \
        if ((v).n == (v).cap) {                                                  \
            (v).cap = (v).cap ? (v).cap * 2 : 8;                                 \
            (v).a = xrealloc((v).a, (v).cap * sizeof *(v).a);                    \
        }                                                                        \
        (v).a[(v).n++] = (__VA_ARGS__);                                          \
    } while (0)

typedef VEC(uint8_t) u8vec;
typedef VEC(uint32_t) u32vec;
typedef VEC(char *) strvec;

/* A NUL-terminated growable string. */
typedef struct { char *s; size_t n, cap; } sbuf;
void sb_put(sbuf *b, const char *s, size_t n);
void sb_puts(sbuf *b, const char *s);
void sb_putc(sbuf *b, char c);
void sb_trunc(sbuf *b, size_t n);
void sb_printf(sbuf *b, const char *fmt, ...);

int has_suffix(const char *s, const char *suf);
int cmp_cstr(const void *a, const void *b); /* qsort comparator for char * */
int u32_cmp(const void *a, const void *b);  /* qsort comparator for uint32_t */
int is_ident_char(int c);
double now_ms(void);
char *read_file(const char *path, size_t *len); /* whole file, NUL-terminated; NULL on error */
void sh_quote(sbuf *b, const char *s);          /* single-quote for /bin/sh */

#endif
