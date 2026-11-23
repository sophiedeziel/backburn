/* String interner: every name is stored once and known by a dense uint32_t id. */

#include "core/intern.h"

interner_t I;
static char *pool_cur;

static size_t pool_left;

static uint64_t fnv1a(const char *s, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t) s[i]) * 1099511628211ULL;
    return h;
}

static const char *pool_copy(const char *s, size_t n) {
    if (n + 1 > pool_left) {
        size_t sz = n + 1 > (1u << 20) ? n + 1 : (1u << 20);
        pool_cur = xrealloc(NULL, sz);
        pool_left = sz;
    }
    char *d = pool_cur;
    memcpy(d, s, n);
    d[n] = 0;
    pool_cur += n + 1;
    pool_left -= n + 1;
    return d;
}

static void intern_grow(void) {
    size_t nc = I.cap ? I.cap * 2 : 1024;
    uint32_t *ns = xcalloc(nc, sizeof *ns);
    for (size_t id = 0; id < I.n; id++) {
        size_t i = I.hash[id] & (nc - 1);
        while (ns[i]) i = (i + 1) & (nc - 1);
        ns[i] = (uint32_t) id + 1;
    }
    free(I.slots);
    I.slots = ns;
    I.cap = nc;
}

uint32_t intern(const char *s, size_t n) {
    if ((I.n + 1) * 2 > I.cap) intern_grow();
    uint64_t h = fnv1a(s, n);
    size_t i = h & (I.cap - 1);
    while (I.slots[i]) {
        uint32_t id = I.slots[i] - 1;
        if (I.hash[id] == h && I.len[id] == n && !memcmp(I.str[id], s, n)) return id;
        i = (i + 1) & (I.cap - 1);
    }
    if (I.n == I.scap) {
        I.scap = I.scap ? I.scap * 2 : 1024;
        I.str = xrealloc(I.str, I.scap * sizeof *I.str);
        I.len = xrealloc(I.len, I.scap * sizeof *I.len);
        I.hash = xrealloc(I.hash, I.scap * sizeof *I.hash);
    }
    uint32_t id = (uint32_t) I.n++;
    I.str[id] = pool_copy(s, n);
    I.len[id] = (uint32_t) n;
    I.hash[id] = h;
    I.slots[i] = id + 1;
    return id;
}

uint32_t intern_cstr(const char *s) { return intern(s, strlen(s)); }

int64_t find_name(const char *s, size_t n) {
    if (!I.cap) return -1;
    uint64_t h = fnv1a(s, n);
    size_t i = h & (I.cap - 1);
    while (I.slots[i]) {
        uint32_t id = I.slots[i] - 1;
        if (I.hash[id] == h && I.len[id] == n && !memcmp(I.str[id], s, n)) return id;
        i = (i + 1) & (I.cap - 1);
    }
    return -1;
}

/* Words the visitor compares against, interned first so their ids are
 * 0..W_N-1 and a call's name is classified with an integer compare. */
static const char *const WORDS[] = {
    "private", "protected", "public", "private_class_method", "public_class_method",
    "module_function", "include", "prepend", "extend",
    "send", "__send__", "public_send", "try", "try!", "method", "public_method",
    "instance_method", "public_instance_method", "respond_to?", "alias_method",
    "define_method",
    "const_get", "constantize", "safe_constantize", "const_defined?",
    "class_eval", "module_eval", "instance_eval", "eval", "runner", "require", NULL};

/* Interns WORDS first, so their ids are 0..W_N-1 (see the W_ enum). */
void intern_words(void) {
    for (int k = 0; WORDS[k]; k++) intern_cstr(WORDS[k]);
}
