/* String interner: every name is stored once and known by a dense uint32_t id. */
#ifndef BACKBURN_CORE_INTERN_H
#define BACKBURN_CORE_INTERN_H

#include "core/util.h"

/* FNV-1a 64, open addressing, linear probing, doubled at 50% load.
 * intern() inserts; find_name() only looks, so analysis never grows the
 * table with failed lookups. */
typedef struct {
    uint32_t *slots; /* id + 1, 0 = empty */
    size_t cap;
    const char **str;
    uint32_t *len;
    uint64_t *hash;
    size_t n, scap;
} interner_t;
extern interner_t I;

uint32_t intern(const char *s, size_t n);
uint32_t intern_cstr(const char *s);
int64_t find_name(const char *s, size_t n); /* -1 when absent */
void intern_words(void);

#define S(id) (I.str[id])
#define SLEN(id) (I.len[id])

/* Words the visitor compares call names against; intern_words() gives
 * them these ids, so a call's name is classified with an integer compare. */
enum {
    W_PRIVATE, W_PROTECTED, W_PUBLIC, W_PRIVATE_CLASS_METHOD, W_PUBLIC_CLASS_METHOD,
    W_MODULE_FUNCTION, W_INCLUDE, W_PREPEND, W_EXTEND,
    W_SEND, W___SEND__, W_PUBLIC_SEND, W_TRY, W_TRY_BANG, W_METHOD, W_PUBLIC_METHOD,
    W_INSTANCE_METHOD, W_PUBLIC_INSTANCE_METHOD, W_RESPOND_TO, W_ALIAS_METHOD,
    W_DEFINE_METHOD,
    W_CONST_GET, W_CONSTANTIZE, W_SAFE_CONSTANTIZE, W_CONST_DEFINED,
    W_CLASS_EVAL, W_MODULE_EVAL, W_INSTANCE_EVAL, W_EVAL, W_RUNNER, W_REQUIRE, W_N
};

#endif
