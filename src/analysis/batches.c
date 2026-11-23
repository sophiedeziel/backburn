/* Removal batches: union-find over dead definitions, so each batch can be deleted on its own. */

#include "analysis/batches.h"
#include "core/intern.h"
#include "analysis/graph.h"
#include "analysis/analysis.h"

batch_vec BATCHES;

static uint32_t *UF;

static uint32_t uf_find(uint32_t x) {
    while (UF[x] != x) x = UF[x] = UF[UF[x]];
    return x;
}

static void uf_join(uint32_t a, uint32_t b) {
    a = uf_find(a), b = uf_find(b);
    if (a != b) UF[a < b ? b : a] = a < b ? a : b;
}

static int member_cmp(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *) a, y = *(const uint32_t *) b;
    int ux = D.a[x].reason == R_NEVER, uy = D.a[y].reason == R_NEVER;
    if (ux != uy) return uy - ux;
    return def_loc_cmp(x, y);
}

static int batch_cmp(const void *a, const void *b) {
    const batch_t *x = a, *y = b;
    int tx = x->test_files.n > 0, ty = y->test_files.n > 0;
    if (tx != ty) return tx - ty;
    if (x->lines != y->lines) return x->lines > y->lines ? -1 : 1;
    return def_loc_cmp(x->members.a[0], y->members.a[0]);
}

typedef struct { uint32_t file, a, b; } span_t;

static int span_cmp(const void *x, const void *y) {
    const span_t *p = x, *q = y;
    if (p->file != q->file) return p->file < q->file ? -1 : 1;
    if (p->a != q->a) return p->a < q->a ? -1 : 1;
    return p->b < q->b ? -1 : p->b > q->b;
}

void build_batches(void) {
    UF = xcalloc(D.n + 1, sizeof *UF);
    for (uint32_t i = 0; i < D.n; i++) UF[i] = i;
    for (uint32_t d = 1; d < D.n; d++) {
        if (D.a[d].live) continue;
        for (size_t k = 0; k < D.a[d].refs.n; k++) {
            const ref_t *r = &D.a[d].refs.a[k];
            if (r->name >= NN) continue;
            CSR_FOR(*targets_of(r->is_const), r->name, t) if (!D.a[t].live && t) uf_join(d, t);
        }
        const def_t *x = &D.a[d];
        if (x->kind == K_CLASS || x->kind == K_MODULE) {
            CSR_FOR(CLS_BY_QUAL_ALL, x->qual, j) if (!D.a[j].live) uf_join(d, j);
        }
        const char *os;
        size_t on;
        owner_qual(x, &os, &on);
        if (on) {
            int64_t oid = find_name(os, on);
            if (oid >= 0 && oid < NN) CSR_FOR(CLS_BY_QUAL_ALL, (uint32_t) oid, j) if (!D.a[j].live) uf_join(d, j);
        }
    }
    int32_t *root_batch = xcalloc(D.n + 1, sizeof *root_batch);
    for (uint32_t i = 0; i <= D.n; i++) root_batch[i] = -1;
    for (uint32_t d = 1; d < D.n; d++) {
        if (D.a[d].live) continue;
        uint32_t r = uf_find(d);
        if (root_batch[r] < 0) {
            batch_t b;
            memset(&b, 0, sizeof b);
            PUSH(BATCHES, b);
            root_batch[r] = (int32_t) BATCHES.n - 1;
        }
        D.a[d].batch = root_batch[r];
        PUSH(BATCHES.a[root_batch[r]].members, d);
    }
    /* test files that name a member */
    for (size_t k = 0; k < TR.n; k++) {
        const tref_t *t = &TR.a[k];
        if (t->name >= NN) continue;
        CSR_FOR(*targets_of(t->is_const), t->name, d) {
            if (D.a[d].live || D.a[d].batch < 0) continue;
            PUSH(BATCHES.a[D.a[d].batch].test_files, t->file);
        }
    }
    VEC(span_t) spans = {0};
    for (size_t b = 0; b < BATCHES.n; b++) {
        batch_t *B = &BATCHES.a[b];
        qsort(B->members.a, B->members.n, sizeof *B->members.a, member_cmp);
        qsort(B->test_files.a, B->test_files.n, sizeof *B->test_files.a, u32_cmp);
        size_t w = 0;
        for (size_t i = 0; i < B->test_files.n; i++)
            if (!w || B->test_files.a[i] != B->test_files.a[w - 1]) B->test_files.a[w++] = B->test_files.a[i];
        B->test_files.n = w;
        spans.n = 0;
        for (size_t i = 0; i < B->members.n; i++) {
            const def_t *d = &D.a[B->members.a[i]];
            PUSH(spans, (span_t){d->file, d->line, d->end_line < d->line ? d->line : d->end_line});
        }
        qsort(spans.a, spans.n, sizeof *spans.a, span_cmp);
        uint32_t lines = 0, nfiles = 0, cf = UINT32_MAX, ca = 0, cb = 0;
        for (size_t i = 0; i <= spans.n; i++) {
            if (i < spans.n && spans.a[i].file == cf && spans.a[i].a <= cb + 1) {
                if (spans.a[i].b > cb) cb = spans.a[i].b;
                continue;
            }
            if (cf != UINT32_MAX) lines += cb - ca + 1;
            if (i == spans.n) break;
            if (spans.a[i].file != cf) nfiles++;
            cf = spans.a[i].file;
            ca = spans.a[i].a;
            cb = spans.a[i].b;
        }
        B->lines = lines;
        B->nfiles = nfiles;
    }
    free(spans.a);
    qsort(BATCHES.a, BATCHES.n, sizeof *BATCHES.a, batch_cmp);
    for (size_t b = 0; b < BATCHES.n; b++)
        for (size_t i = 0; i < BATCHES.a[b].members.n; i++) D.a[BATCHES.a[b].members.a[i]].batch = (int32_t) b;
    free(root_batch);
}
