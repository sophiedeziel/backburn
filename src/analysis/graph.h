/* The reference graph: name indexes, name-pattern expansion, and class / mixin resolution. */
#ifndef BACKBURN_ANALYSIS_GRAPH_H
#define BACKBURN_ANALYSIS_GRAPH_H

#include "core/model.h"

extern uint32_t NN; /* names known when analysis starts */

/* Compressed rows: items[off[k] .. off[k+1]) belong to key k. */
typedef struct { uint32_t *off, *items; } csr_t;
#define CSR_FOR(c, k, var)                                                            \
    for (uint32_t _i = (c).off[(k)], _e = (c).off[(k) + 1], var; _i < _e && ((var = (c).items[_i]), 1); _i++)

extern csr_t BY_METH, BY_CONST;      /* name id -> method defs / class, module and constant defs */
extern csr_t CLS_BY_QUAL_ALL;        /* qualified name id -> every class / module def (reopenings) */
extern csr_t INC_BY_CLS, INC_BY_RES; /* mixins by including class, by resolved module */
extern int32_t *CLS_BY_QUAL;         /* qualified name id -> preferred class / module def */
extern int32_t *SUPER_DEF;           /* def -> resolved superclass def, or -1 */

void csr_build(csr_t *c, uint32_t nkeys, size_t nitems, uint32_t (*key)(size_t, int *), size_t item_base);
void build_name_indexes(void);
const csr_t *targets_of(int is_const);
uint32_t def_key(const def_t *d); /* name * 2 + is_const: the key a reference to d uses */

void dedupe_refs(def_t *d);
const ref_t *find_ref(const def_t *d, uint32_t name, int is_const);
void owner_qual(const def_t *d, const char **s, size_t *n);

void expand_patterns(void);
void index_classes(void);
void resolve_mixins(void);

#endif
