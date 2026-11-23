/* Marking (Trémaux), namespace liveness, and classification of what stays dead. */

#include "analysis/mark.h"
#include "core/intern.h"
#include "analysis/graph.h"
#include "analysis/roots.h"

void mark(void) {
    uint8_t *done = xcalloc((size_t) NN * 2 + 2, 1);
    u32vec stack = {0};
    for (uint32_t i = 0; i < D.n; i++) {
        int32_t rr = root_for(i);
        D.a[i].root_rule = rr;
        if (rr != NOT_ROOT) {
            D.a[i].live = 1;
            PUSH(stack, i);
        }
    }
    while (stack.n) {
        uint32_t d = stack.a[--stack.n];
        for (size_t k = 0; k < D.a[d].refs.n; k++) {
            ref_t r = D.a[d].refs.a[k];
            size_t key = (size_t) r.name * 2 + r.is_const;
            if (r.name >= NN || done[key]) continue;
            done[key] = 1;
            CSR_FOR(*targets_of(r.is_const), r.name, t) {
                if (D.a[t].live) continue;
                D.a[t].live = 1;
                D.a[t].parent = (int32_t) d;
                D.a[t].pfile = r.file;
                D.a[t].pline = r.line;
                D.a[t].via = VIA_CALLS;
                PUSH(stack, t);
            }
        }
    }
    free(stack.a);
    free(done);
}

/* A class or module is live if a live class, module or constant is nested
 * in it. Methods don't count: a method is live by name alone. */
void mark_namespaces(void) {
    for (uint32_t i = 0; i < D.n; i++) {
        if (!D.a[i].live || !is_constk(D.a[i].kind)) continue;
        const char *q = S(D.a[i].qual);
        size_t L = SLEN(D.a[i].qual);
        uint32_t child = i;
        for (;;) {
            size_t k = L;
            while (k >= 2 && !(q[k - 1] == ':' && q[k - 2] == ':')) k--;
            if (k < 2) break;
            L = k - 2;
            int64_t id = find_name(q, L);
            if (id < 0 || id >= NN) continue;
            uint32_t next = child;
            CSR_FOR(CLS_BY_QUAL_ALL, (uint32_t) id, j) {
                if (!D.a[j].live) {
                    D.a[j].live = 1;
                    D.a[j].parent = (int32_t) child;
                    D.a[j].via = VIA_HOLDS;
                    D.a[j].pfile = D.a[child].file;
                    D.a[j].pline = D.a[child].line;
                }
                next = j;
            }
            child = next;
        }
    }
}

csr_t TR_BY_KEY, DEAD_CALLERS;

static uint32_t *STAMP, GEN;

size_t NCAND;

static uint32_t key_tr(size_t i, int *ok) { *ok = TR.a[i].name < NN; return *ok ? TR.a[i].name * 2 + TR.a[i].is_const : 0; }

/* Dead-callers index: for each (name, kind) key, the dead defs that name it. */
static void build_dead_callers(void) {
    DEAD_CALLERS.off = xcalloc((size_t) NN * 2 + 2, sizeof(uint32_t));
    for (size_t d = 1; d < D.n; d++) {
        if (D.a[d].live) continue;
        for (size_t k = 0; k < D.a[d].refs.n; k++) {
            const ref_t *r = &D.a[d].refs.a[k];
            if (r->name < NN) DEAD_CALLERS.off[r->name * 2 + r->is_const + 2]++;
        }
    }
    for (size_t k = 0; k < (size_t) NN * 2; k++) DEAD_CALLERS.off[k + 2] += DEAD_CALLERS.off[k + 1];
    DEAD_CALLERS.items = xcalloc(DEAD_CALLERS.off[(size_t) NN * 2 + 1] + 1, sizeof(uint32_t));
    for (size_t d = 1; d < D.n; d++) {
        if (D.a[d].live) continue;
        for (size_t k = 0; k < D.a[d].refs.n; k++) {
            const ref_t *r = &D.a[d].refs.a[k];
            if (r->name < NN) DEAD_CALLERS.items[DEAD_CALLERS.off[r->name * 2 + r->is_const + 1]++] = (uint32_t) d;
        }
    }
}

static int in_dead_cycle(uint32_t t) {
    GEN++;
    u32vec st = {0};
    PUSH(st, t);
    int first = 1, found = 0;
    while (st.n && !found) {
        uint32_t u = st.a[--st.n];
        if (!first && STAMP[u] == GEN) continue;
        STAMP[u] = GEN;
        first = 0;
        for (size_t k = 0; k < D.a[u].refs.n && !found; k++) {
            const ref_t *r = &D.a[u].refs.a[k];
            if (r->name >= NN) continue;
            CSR_FOR(*targets_of(r->is_const), r->name, v) {
                if (D.a[v].live || v == u) continue;
                if (v == t) {
                    found = 1;
                    break;
                }
                if (STAMP[v] != GEN) PUSH(st, v);
            }
        }
    }
    free(st.a);
    return found;
}

void classify(void) {
    uint32_t *lcnt = xcalloc((size_t) NN * 2 + 2, sizeof *lcnt), *dcnt = xcalloc((size_t) NN * 2 + 2, sizeof *dcnt);
    for (size_t d = 0; d < D.n; d++)
        for (size_t k = 0; k < D.a[d].refs.n; k++) {
            const ref_t *r = &D.a[d].refs.a[k];
            if (r->name >= NN) continue;
            (D.a[d].live ? lcnt : dcnt)[r->name * 2 + r->is_const]++;
        }
    csr_build(&TR_BY_KEY, NN * 2, TR.n, key_tr, 0);
    build_dead_callers();
    STAMP = xcalloc(D.n + 1, sizeof *STAMP);
    NCAND = 0;
    for (size_t t = 1; t < D.n; t++) {
        def_t *d = &D.a[t];
        uint32_t key = def_key(d);
        int self = find_ref(d, d->name, is_constk(d->kind)) != NULL;
        d->in_live = lcnt[key] - (d->live && self);
        d->in_dead = dcnt[key] - (!d->live && self);
        d->test_ref = TR_BY_KEY.off[key + 1] > TR_BY_KEY.off[key];
        if (d->live) {
            d->reason = R_LIVE;
            continue;
        }
        NCAND++;
        d->reason = d->in_dead == 0 ? R_NEVER : in_dead_cycle((uint32_t) t) ? R_CYCLE : R_DEAD_ONLY;
    }
    free(lcnt);
    free(dcnt);
}

void dead_callers(uint32_t t, u32vec *out) {
    const def_t *d = &D.a[t];
    uint32_t key = def_key(d);
    if (d->name >= NN) return;
    for (uint32_t k = DEAD_CALLERS.off[key]; k < DEAD_CALLERS.off[key + 1]; k++) {
        uint32_t c = DEAD_CALLERS.items[k];
        if (c != t && (!out->n || out->a[out->n - 1] != c)) PUSH(*out, c);
    }
}

void test_files_of(uint32_t t, u32vec *out) {
    const def_t *d = &D.a[t];
    uint32_t key = def_key(d);
    if (d->name >= NN) return;
    CSR_FOR(TR_BY_KEY, key, k) PUSH(*out, TR.a[k].file);
    qsort(out->a, out->n, sizeof *out->a, u32_cmp);
    size_t w = 0;
    for (size_t i = 0; i < out->n; i++)
        if (!w || out->a[i] != out->a[w - 1]) out->a[w++] = out->a[i];
    out->n = w;
}
