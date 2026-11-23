/* The analysis pipeline, definition ordering, and stable ids. */

#include "analysis/analysis.h"
#include "core/intern.h"
#include "core/model.h"
#include "cli/options.h"
#include "index/walk.h"
#include "analysis/graph.h"
#include "analysis/roots.h"
#include "analysis/mark.h"
#include "analysis/batches.h"

uint32_t *FILE_RANK; /* files sorted by path */

int def_loc_cmp(uint32_t a, uint32_t b) {
    const def_t *x = &D.a[a], *y = &D.a[b];
    if (x->file != y->file) return FILE_RANK[x->file] < FILE_RANK[y->file] ? -1 : 1;
    if (x->line != y->line) return x->line < y->line ? -1 : 1;
    return a < b ? -1 : a > b;
}

int def_loc_qcmp(const void *a, const void *b) { return def_loc_cmp(*(const uint32_t *) a, *(const uint32_t *) b); }

char *ROOT_ABS;

double ELAPSED_MS;

size_t NREFS;

static int file_rank_cmp(const void *a, const void *b) {
    return strcmp(FILES.a[*(const uint32_t *) a], FILES.a[*(const uint32_t *) b]);
}

void analyse(void) {
    double t0 = now_ms();
    index_tree(O.root);
    build_name_indexes();
    expand_patterns();
    NREFS = 0;
    for (size_t i = 0; i < D.n; i++) {
        dedupe_refs(&D.a[i]);
        NREFS += D.a[i].refs.n;
    }
    index_classes();
    resolve_mixins();
    prepare_roots();
    mark();
    mark_namespaces();
    classify();
    uint32_t *order = xcalloc(FILES.n + 1, sizeof *order);
    for (uint32_t f = 0; f < FILES.n; f++) order[f] = f;
    qsort(order, FILES.n, sizeof *order, file_rank_cmp);
    FILE_RANK = xcalloc(FILES.n + 1, sizeof *FILE_RANK);
    for (uint32_t r = 0; r < FILES.n; r++) FILE_RANK[order[r]] = r;
    free(order);
    build_batches();
    ELAPSED_MS = now_ms() - t0;
}

static uint8_t *ID_MULTI; /* per def: its (kind word, qual) is defined in several files */

void compute_ids(void) {
    ID_MULTI = xcalloc(D.n + 1, 1);
    int32_t *first[4];
    for (int k = 0; k < 4; k++) {
        first[k] = xcalloc(NN + 1, sizeof *first[k]);
        for (uint32_t j = 0; j <= NN; j++) first[k][j] = -1;
    }
    uint8_t *multi[4];
    for (int k = 0; k < 4; k++) multi[k] = xcalloc(NN + 1, 1);
    for (size_t i = 1; i < D.n; i++) {
        const def_t *d = &D.a[i];
        int k = is_meth(d->kind) ? 0 : d->kind == K_CLASS ? 1 : d->kind == K_MODULE ? 2 : 3;
        if (first[k][d->qual] < 0) first[k][d->qual] = (int32_t) d->file;
        else if ((uint32_t) first[k][d->qual] != d->file) multi[k][d->qual] = 1;
    }
    for (size_t i = 1; i < D.n; i++) {
        const def_t *d = &D.a[i];
        int k = is_meth(d->kind) ? 0 : d->kind == K_CLASS ? 1 : d->kind == K_MODULE ? 2 : 3;
        ID_MULTI[i] = multi[k][d->qual];
    }
    for (int k = 0; k < 4; k++) free(first[k]), free(multi[k]);
}

void def_id(sbuf *b, uint32_t i) {
    const def_t *d = &D.a[i];
    if (d->kind == K_LOAD) {
        sb_puts(b, "load:<load time>");
        return;
    }
    sb_printf(b, "%s:%s", kind_word(d->kind), S(d->qual));
    if (ID_MULTI && ID_MULTI[i]) sb_printf(b, "@%s", FILES.a[d->file]);
}
