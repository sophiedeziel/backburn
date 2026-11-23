/* Memory, growable vectors, string buffers and small file helpers. */
#include <ctype.h>
#include <time.h>

#include "core/util.h"

static void die_oom(void) {
    fputs("backburn: out of memory\n", stderr);
    exit(70);
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) die_oom();
    return q;
}

void *xcalloc(size_t n, size_t s) {
    void *q = calloc(n ? n : 1, s ? s : 1);
    if (!q) die_oom();
    return q;
}

char *xstrndup(const char *s, size_t n) {
    char *d = xrealloc(NULL, n + 1);
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

char *xstrdup(const char *s) { return xstrndup(s, strlen(s)); }

static void sb_grow(sbuf *b, size_t extra) {
    if (b->n + extra + 1 > b->cap) {
        size_t c = b->cap ? b->cap : 64;
        while (c < b->n + extra + 1) c *= 2;
        b->s = xrealloc(b->s, c);
        b->cap = c;
    }
}

void sb_put(sbuf *b, const char *s, size_t n) {
    sb_grow(b, n);
    if (n) memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = 0;
}

void sb_puts(sbuf *b, const char *s) { sb_put(b, s, strlen(s)); }

void sb_putc(sbuf *b, char c) { sb_put(b, &c, 1); }

void sb_trunc(sbuf *b, size_t n) {
    sb_grow(b, 0);
    b->n = n;
    b->s[n] = 0;
}

void sb_printf(sbuf *b, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int k = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (k > 0) {
        sb_grow(b, (size_t) k);
        vsnprintf(b->s + b->n, (size_t) k + 1, fmt, ap2);
        b->n += (size_t) k;
    }
    va_end(ap2);
}

int has_suffix(const char *s, const char *suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && !memcmp(s + a - b, suf, b);
}

int cmp_cstr(const void *a, const void *b) {
    return strcmp(*(char *const *) a, *(char *const *) b);
}

int u32_cmp(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *) a, y = *(const uint32_t *) b;
    return x < y ? -1 : x > y;
}

int is_ident_char(int c) { return isalnum(c) || c == '_'; }

double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    sbuf b = {0};
    char tmp[65536];
    size_t k;
    while ((k = fread(tmp, 1, sizeof tmp, f)) > 0) sb_put(&b, tmp, k);
    fclose(f);
    if (!b.s) sb_grow(&b, 0), b.s[0] = 0;
    *len = b.n;
    return b.s;
}

/* Single-quote a string for /bin/sh. */
void sh_quote(sbuf *b, const char *s) {
    sb_putc(b, '\'');
    for (; *s; s++) {
        if (*s == '\'') sb_puts(b, "'\\''");
        else sb_putc(b, *s);
    }
    sb_putc(b, '\'');
}
