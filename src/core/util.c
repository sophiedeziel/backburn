/* Memory, growable vectors, string buffers and small file helpers. */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

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

/* The whole file, NUL-terminated, in one read when fstat gives its size. */
char *read_file(const char *path, size_t *len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    size_t cap = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size > 0 ? (size_t) st.st_size : 65536, n = 0;
    char *b = xrealloc(NULL, cap + 1);
    for (;;) {
        char probe; /* at the expected size: probe for more before growing */
        char *at = n < cap ? b + n : &probe;
        ssize_t k = read(fd, at, n < cap ? cap - n : 1);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break;
        if (at == &probe) { /* the file grew, or fstat gave no size */
            b = xrealloc(b, (cap *= 2) + 1);
            b[n] = probe;
        }
        n += (size_t) k;
    }
    close(fd);
    b[n] = 0;
    *len = n;
    return b;
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
