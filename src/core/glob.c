/* Byte-wise fnmatch for the globs in roots files, presets and computed names. */
#include <fnmatch.h>
#include <string.h>

#include "core/glob.h"

void glob_compile(glob_pat *g, const char *pat) {
    size_t n = strlen(pat), after_star = 0;
    memset(g, 0, sizeof *g);
    g->pat = pat;
    g->len = n;
    g->head = strcspn(pat, "*?\\[");
    g->exact = g->head == n;
    for (size_t i = 0; i < n; i++) {
        if (pat[i] == '[') g->libc = 1;
        else if (pat[i] == '*') g->star = 1, after_star = i + 1;
        else if (pat[i] == '\\' && ++i == n) g->libc = 1;
    }
    /* the bytes after the last unescaped `*` must end the string, if they are all literal */
    if (g->star && strcspn(pat + after_star, "?\\[") == n - after_star) g->tail = n - after_star;
}

/* `*` with one backtrack point: on a mismatch, let the last `*` absorb one more byte. */
static int match_bytes(const char *p, const char *s, const char *end) {
    const char *star_p = NULL, *star_s = NULL;
    while (s < end) {
        if (*p == '*') {
            while (*p == '*') p++;
            if (!*p) return 1;
            star_p = p;
            star_s = s;
            continue;
        }
        if (*p == '?') {
            p++;
            s++;
            continue;
        }
        char c = *p;
        const char *next = p + 1;
        if (c == '\\') { /* never last: glob_compile sends a trailing backslash to fnmatch */
            c = p[1];
            next = p + 2;
        }
        if (c && c == *s) {
            p = next;
            s++;
            continue;
        }
        if (!star_p) return 0;
        p = star_p;
        s = ++star_s;
    }
    while (*p == '*') p++;
    return !*p;
}

int glob_match(const glob_pat *g, const char *s, size_t n) {
    if (g->libc) return !fnmatch(g->pat, s, 0);
    if (g->exact) return n == g->head && !memcmp(g->pat, s, n);
    if (n < g->head || memcmp(g->pat, s, g->head)) return 0;
    if (g->tail && (n < g->head + g->tail || memcmp(g->pat + g->len - g->tail, s + n - g->tail, g->tail)))
        return 0;
    return match_bytes(g->pat + g->head, s + g->head, s + n);
}

int glob_match_str(const char *pat, const char *s) {
    glob_pat g;
    glob_compile(&g, pat);
    return glob_match(&g, s, strlen(s));
}
