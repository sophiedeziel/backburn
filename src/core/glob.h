/* Globs for names and paths: fnmatch(pat, s, 0) without fnmatch's cost.
 *
 * Backburn never calls setlocale, so fnmatch runs in the C locale and compares
 * bytes: `*` matches any run of bytes (also `/`, `::` and a leading `.`), `?`
 * one byte, and `\x` a literal x. glob_match does the same in a byte loop, after
 * cheap checks on the literal bytes every match must hold: the pattern's head, its
 * tail and its longest run in between. Patterns with brackets, or ending in a lone
 * backslash, go to fnmatch itself. */
#ifndef BACKBURN_CORE_GLOB_H
#define BACKBURN_CORE_GLOB_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *pat; /* as written, NUL-terminated; not owned */
    size_t len;
    size_t head;     /* literal bytes before the first wildcard or escape: every match starts with them */
    size_t tail;     /* literal bytes after the last `*`: every match ends with them (0: unknown) */
    const char *mid; /* longest literal run between the first and last `*`, midn bytes long */
    size_t midn;     /* (0: none); every match holds it between its head and its tail */
    uint8_t exact;   /* no wildcard or escape: plain equality */
    uint8_t star;    /* has a `*` */
    uint8_t libc;    /* brackets or a trailing backslash: ask fnmatch */
} glob_pat;

void glob_compile(glob_pat *g, const char *pat);

/* Same answer as !fnmatch(g->pat, s, 0), where s is n bytes followed by a NUL. */
int glob_match(const glob_pat *g, const char *s, size_t n);

/* glob_compile then glob_match, for a pattern used once. */
int glob_match_str(const char *pat, const char *s);

#endif
