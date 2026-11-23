/* The reference graph: name indexes, name-pattern expansion, and class / mixin resolution. */
#include <fnmatch.h>

#include "analysis/graph.h"
#include "core/intern.h"

uint32_t NN; /* names known when analysis starts */


csr_t BY_METH, BY_CONST, CLS_BY_QUAL_ALL, INC_BY_CLS, INC_BY_RES;

int32_t *CLS_BY_QUAL; /* preferred class/module def for a qualified name */

int32_t *SUPER_DEF;   /* resolved superclass def, per def */

void csr_build(csr_t *c, uint32_t nkeys, size_t nitems, uint32_t (*key)(size_t, int *), size_t item_base) {
    (void) item_base;
    c->off = xcalloc(nkeys + 2, sizeof *c->off);
    for (size_t i = 0; i < nitems; i++) {
        int ok = 1;
        uint32_t k = key(i, &ok);
        if (ok) c->off[k + 2]++;
    }
    for (uint32_t k = 0; k < nkeys; k++) c->off[k + 2] += c->off[k + 1];
    c->items = xcalloc(c->off[nkeys + 1] + 1, sizeof *c->items);
    for (size_t i = 0; i < nitems; i++) {
        int ok = 1;
        uint32_t k = key(i, &ok);
        if (ok) c->items[c->off[k + 1]++] = (uint32_t) i;
    }
}

static uint32_t key_meth(size_t i, int *ok) { *ok = is_meth(D.a[i].kind); return D.a[i].name; }

static uint32_t key_const(size_t i, int *ok) { *ok = is_constk(D.a[i].kind); return D.a[i].name; }

static uint32_t key_cls_qual(size_t i, int *ok) {
    *ok = D.a[i].kind == K_CLASS || D.a[i].kind == K_MODULE;
    return D.a[i].qual;
}

static uint32_t key_inc_cls(size_t i, int *ok) { *ok = 1; return INC.a[i].cls; }

static uint32_t key_inc_res(size_t i, int *ok) {
    *ok = INC.a[i].res >= 0;
    return *ok ? D.a[INC.a[i].res].qual : 0;
}

const csr_t *targets_of(int is_const) { return is_const ? &BY_CONST : &BY_METH; }

uint32_t def_key(const def_t *d) { return d->name * 2 + (is_constk(d->kind) ? 1 : 0); }

static int ref_cmp(const void *a, const void *b) {
    const ref_t *x = a, *y = b;
    if (x->is_const != y->is_const) return x->is_const - y->is_const;
    if (x->name != y->name) return x->name < y->name ? -1 : 1;
    if (x->file != y->file) return x->file < y->file ? -1 : 1;
    return x->line < y->line ? -1 : x->line > y->line;
}

void dedupe_refs(def_t *d) {
    if (d->refs.n < 2) return;
    qsort(d->refs.a, d->refs.n, sizeof *d->refs.a, ref_cmp);
    size_t w = 1;
    for (size_t i = 1; i < d->refs.n; i++)
        if (d->refs.a[i].name != d->refs.a[w - 1].name || d->refs.a[i].is_const != d->refs.a[w - 1].is_const)
            d->refs.a[w++] = d->refs.a[i];
    d->refs.n = w;
}

const ref_t *find_ref(const def_t *d, uint32_t name, int is_const) {
    size_t lo = 0, hi = d->refs.n;
    ref_t key = {name, 0, 0, (uint8_t) is_const};
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        const ref_t *r = &d->refs.a[mid];
        int c = r->is_const != key.is_const ? r->is_const - key.is_const : (r->name < name ? -1 : r->name > name);
        if (!c) return r;
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

void owner_qual(const def_t *d, const char **s, size_t *n) {
    const char *q = S(d->qual);
    size_t L = SLEN(d->qual);
    *s = q;
    *n = 0;
    if (is_meth(d->kind)) {
        const char *cut = strrchr(q, '#');
        if (!cut) cut = strrchr(q, '.');
        if (cut) *n = (size_t) (cut - q);
    } else {
        for (size_t k = L; k >= 2; k--)
            if (q[k - 1] == ':' && q[k - 2] == ':') {
                *n = k - 2;
                break;
            }
    }
}

static int pref_cmp(const void *a, const void *b) {
    const pref_t *x = a, *y = b;
    if (x->is_const != y->is_const) return x->is_const - y->is_const;
    return strcmp(S(x->pat), S(y->pat));
}

void expand_patterns(void) {
    if (!PREF.n) return;
    u32vec mnames = {0}, cnames = {0};
    for (uint32_t k = 0; k < NN; k++) {
        if (BY_METH.off[k + 1] > BY_METH.off[k]) PUSH(mnames, k);
        if (BY_CONST.off[k + 1] > BY_CONST.off[k]) PUSH(cnames, k);
    }
    qsort(PREF.a, PREF.n, sizeof *PREF.a, pref_cmp);
    for (size_t i = 0; i < PREF.n;) {
        size_t j = i;
        while (j < PREF.n && PREF.a[j].is_const == PREF.a[i].is_const && PREF.a[j].pat == PREF.a[i].pat) j++;
        const char *pat = S(PREF.a[i].pat);
        size_t plen = strcspn(pat, "*\\");
        const u32vec *names = PREF.a[i].is_const ? &cnames : &mnames;
        for (size_t k = 0; k < names->n; k++) {
            uint32_t nm = names->a[k];
            if (strncmp(S(nm), pat, plen) || fnmatch(pat, S(nm), 0)) continue;
            for (size_t o = i; o < j; o++) {
                const pref_t *pr = &PREF.a[o];
                if (pr->owner < 0) PUSH(TR, (tref_t){nm, pr->file, pr->is_const});
                else PUSH(D.a[pr->owner].refs, (ref_t){nm, pr->file, pr->line, pr->is_const});
            }
        }
        i = j;
    }
    free(mnames.a);
    free(cnames.a);
}

/* by_qual maps the first nq qualified name ids to a class / module def, or -1. */
static int32_t lookup_cls(const int32_t *by_qual, uint32_t nq, const char *s, size_t n) {
    int64_t id = find_name(s, n);
    return id >= 0 && id < nq ? by_qual[id] : -1;
}

/* A class is never its own superclass: `class Admin::Thing < ::Thing`
 * must not resolve ::Thing to Admin::Thing. */
static int32_t lookup_other(const int32_t *by_qual, uint32_t nq, const char *s, size_t n, int64_t self) {
    int32_t r = lookup_cls(by_qual, nq, s, n);
    return r >= 0 && (int64_t) D.a[r].qual == self ? -1 : r;
}

/* Ruby's constant lookup, without ancestors: Name in each scope of the nesting
 * (Module.nesting, innermost first, newline-separated), exactly, then at the top level. */
static int32_t resolve(const int32_t *by_qual, uint32_t nq, uint32_t name, int32_t nest, int64_t self) {
    const char *nm = S(name);
    if (nest >= 0) {
        sbuf b = {0};
        for (const char *p = S(nest), *end = p + SLEN(nest); p < end;) {
            const char *nl = memchr(p, '\n', (size_t) (end - p));
            if (!nl) nl = end;
            sb_trunc(&b, 0);
            sb_put(&b, p, (size_t) (nl - p));
            sb_put(&b, "::", 2);
            sb_put(&b, nm, SLEN(name));
            int32_t r = lookup_other(by_qual, nq, b.s, b.n, self);
            if (r >= 0) {
                free(b.s);
                return r;
            }
            p = nl + 1;
        }
        free(b.s);
    }
    return lookup_other(by_qual, nq, nm, SLEN(name), self);
}

/* `def Name.x` inside a namespace: look Name up like any constant, now that
 * every file is indexed, and requalify the def by the class found, or by Name
 * at the top level when no indexed class matches. Its self-sends move with it.
 * Runs before the name indexes, so the new names are counted in NN. */
void place_singleton_defs(void) {
    if (!SRECV.n) return;
    uint32_t nq = (uint32_t) I.n;
    int32_t *by_qual = xcalloc(nq + 1, sizeof *by_qual);
    for (uint32_t k = 0; k < nq; k++) by_qual[k] = -1;
    for (size_t i = 0; i < D.n; i++)
        if (D.a[i].kind == K_CLASS || D.a[i].kind == K_MODULE) by_qual[D.a[i].qual] = (int32_t) i;
    uint32_t *placed = xcalloc(D.n, sizeof *placed); /* new owner + 1, per moved def */
    sbuf b = {0};
    for (size_t k = 0; k < SRECV.n; k++) {
        const srecv_t *s = &SRECV.a[k];
        def_t *d = &D.a[s->def];
        int32_t r = resolve(by_qual, nq, s->recv, s->nest, -1);
        uint32_t owner = r >= 0 ? D.a[r].qual : s->recv;
        sb_trunc(&b, 0);
        sb_put(&b, S(owner), SLEN(owner));
        sb_putc(&b, '.');
        sb_put(&b, S(d->name), SLEN(d->name));
        d->qual = intern(b.s, b.n);
        placed[s->def] = owner + 1;
    }
    /* every self-send owned by a moved def is in its body, where self is the receiver */
    for (size_t j = 0; j < DYN.n; j++)
        if (DYN.a[j].self_ns >= 0 && placed[DYN.a[j].owner]) DYN.a[j].self_ns = (int32_t) (placed[DYN.a[j].owner] - 1);
    free(b.s);
    free(placed);
    free(by_qual);
}

void index_classes(void) {
    CLS_BY_QUAL = xcalloc(NN + 1, sizeof *CLS_BY_QUAL);
    for (uint32_t k = 0; k <= NN; k++) CLS_BY_QUAL[k] = -1;
    for (size_t i = 0; i < D.n; i++) {
        def_t *d = &D.a[i];
        if (d->kind != K_CLASS && d->kind != K_MODULE) continue;
        int32_t cur = CLS_BY_QUAL[d->qual];
        if (cur < 0 || (D.a[cur].super_ < 0 && d->super_ >= 0)) CLS_BY_QUAL[d->qual] = (int32_t) i;
    }
    csr_build(&CLS_BY_QUAL_ALL, NN, D.n, key_cls_qual, 0);
    SUPER_DEF = xcalloc(D.n, sizeof *SUPER_DEF);
    for (size_t i = 0; i < D.n; i++) {
        SUPER_DEF[i] = -1;
        if (D.a[i].kind == K_CLASS && D.a[i].super_ >= 0)
            SUPER_DEF[i] = resolve(CLS_BY_QUAL, NN, (uint32_t) D.a[i].super_, D.a[i].super_top ? -1 : D.a[i].outer, D.a[i].qual);
    }
}

void resolve_mixins(void) {
    for (size_t i = 0; i < INC.n; i++) INC.a[i].res = resolve(CLS_BY_QUAL, NN, INC.a[i].mod, INC.a[i].nest, -1);
    csr_build(&INC_BY_CLS, NN, INC.n, key_inc_cls, 0);
    csr_build(&INC_BY_RES, NN, INC.n, key_inc_res, 0);
}

/* by_meth / by_const: every definition with a given name, in any class. */
void build_name_indexes(void) {
    NN = (uint32_t) I.n;
    csr_build(&BY_METH, NN, D.n, key_meth, 0);
    csr_build(&BY_CONST, NN, D.n, key_const, 0);
}
