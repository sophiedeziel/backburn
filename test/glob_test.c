/* glob_match must answer exactly as fnmatch(pat, s, 0) in the C locale.
 * Compares them on fixed cases and on random patterns and strings drawn from the
 * bytes that matter (`*`, `?`, `\`, brackets, `/`, `::`, `#`, `.`). Run by `make test`. */
#include <fnmatch.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/glob.h"

static int failures;

static void check(const char *pat, const char *s) {
    glob_pat g;
    glob_compile(&g, pat);
    size_t n = strlen(s);
    int want = !fnmatch(pat, s, 0), got = glob_match(&g, s, n);
    if (want != got && failures++ < 20)
        fprintf(stderr, "glob_match(\"%s\", \"%s\") = %d, fnmatch says %d\n", pat, s, got, want);
    /* callers narrow candidates by head and tail, so every match must start and end with them */
    if (want && (n < g.head + g.tail || memcmp(s, pat, g.head) || memcmp(s + n - g.tail, pat + g.len - g.tail, g.tail)) &&
        failures++ < 20)
        fprintf(stderr, "\"%s\" matches \"%s\" but not its head (%zu) or tail (%zu)\n", s, pat, g.head, g.tail);
}

static uint64_t state = 0x9e3779b97f4a7c15u; /* fixed seed: the same cases on every run */

static uint32_t next(uint32_t bound) {
    state = state * 6364136223846793005u + 1442695040888963407u;
    return (uint32_t) (state >> 33) % bound;
}

static void random_string(char *out, const char *alphabet, uint32_t max) {
    size_t a = strlen(alphabet);
    uint32_t n = next(max + 1);
    for (uint32_t i = 0; i < n; i++) out[i] = alphabet[next((uint32_t) a)];
    out[n] = 0;
}

/* A string the pattern likely matches: each `*` becomes 0-3 random bytes, `?` one, `\x` x. */
static void string_for(char *out, const char *pat) {
    size_t o = 0;
    for (const char *p = pat; *p; p++) {
        if (*p == '*')
            for (uint32_t k = next(4); k; k--) out[o++] = "ab:/#."[next(6)];
        else if (*p == '?') out[o++] = "ab:/#."[next(6)];
        else if (*p == '\\' && p[1]) out[o++] = *++p;
        else out[o++] = *p;
    }
    out[o] = 0;
}

int main(void) {
    static const char *const fixed[][2] = {
        {"*Controller", "UsersController"}, {"*Controller", "Controller"}, {"*Controller", "UsersControllers"},
        {"public*", "public"}, {"*#to_xml", "Order#to_xml"}, {"*#to_xml", "Order.to_xml"},
        {"Admin::*", "Admin::Thing#x"}, {"*/config/*", "engines/a/config/b.rb"}, {"*.rake", "lib/tasks/a.rake"},
        {"a*b*c", "aXbYc"}, {"a*b*c", "abcab"}, {"a*b", "ab"}, {"*a*", ""}, {"", ""}, {"", "a"},
        {"*", ""}, {"**", "x"}, {"?", ""}, {"?", "é"}, {"a?c", "abc"}, {"\\*", "*"}, {"\\*", "a"},
        {"a\\", "a\\"}, {"a\\", "a"}, {"[ab]c", "bc"}, {"*[!a]", "xb"}, {"\\[x]", "[x]"}, {"*\\?", "a?"},
        {"foo_*_path", "foo_bar_path"}, {"foo_*_path", "foo__path"}, {"foo_*_path", "foo_path"},
    };
    for (size_t i = 0; i < sizeof fixed / sizeof *fixed; i++) check(fixed[i][0], fixed[i][1]);

    char pat[16], s[64];
    for (int i = 0; i < 1000000; i++) {
        random_string(pat, "ab*?*\\[]!:/#.", 9);
        random_string(s, "ab*?\\[]:/#.", 12);
        check(pat, s);
        string_for(s, pat);
        check(pat, s);
    }
    if (failures) {
        fprintf(stderr, "%d case(s) differ from fnmatch\n", failures);
        return 1;
    }
    return 0;
}
