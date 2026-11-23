/* explain NAME: why is this live, or why is it dead? */
#include <errno.h>

#include "commands/commands.h"
#include "core/intern.h"
#include "core/json.h"
#include "cli/options.h"
#include "analysis/graph.h"
#include "analysis/roots.h"
#include "analysis/mark.h"
#include "analysis/analysis.h"
#include "output/report.h"

/* Find definitions by qualified name, by id (kind:Name[@path]), or by file:line. */
static void find_defs(const char *name, u32vec *out) {
    const char *q = name;
    const char *colon = strchr(name, ':');
    if (colon && colon[1] != ':' && colon > name && strncmp(name, "file:", 5)) {
        size_t kn = (size_t) (colon - name);
        if ((kn == 6 && !strncmp(name, "method", 6)) || (kn == 5 && !strncmp(name, "class", 5)) ||
            (kn == 6 && !strncmp(name, "module", 6)) || (kn == 8 && !strncmp(name, "constant", 8)))
            q = colon + 1;
    }
    size_t qn = strcspn(q, "@");
    int64_t id = find_name(q, qn);
    if (id >= 0)
        for (uint32_t i = 1; i < D.n; i++)
            if (D.a[i].qual == (uint32_t) id && (q[qn] != '@' || !strcmp(FILES.a[D.a[i].file], q + qn + 1))) PUSH(*out, i);
    if (out->n) return;
    /* file:line, the innermost definition spanning it */
    const char *lc = strrchr(name, ':');
    if (lc && lc[1] && strspn(lc + 1, "0123456789") == strlen(lc + 1)) {
        size_t fn = (size_t) (lc - name);
        errno = 0;
        unsigned long ul = strtoul(lc + 1, NULL, 10);
        if (errno || ul > UINT32_MAX) return;
        uint32_t line = (uint32_t) ul;
        int32_t best = -1;
        for (uint32_t i = 1; i < D.n; i++) {
            const def_t *d = &D.a[i];
            const char *path = FILES.a[d->file];
            if (strlen(path) != fn || strncmp(path, name, fn)) continue;
            if (d->line <= line && line <= (d->end_line < d->line ? d->line : d->end_line))
                if (best < 0 || D.a[best].line <= d->line) best = (int32_t) i;
        }
        if (best >= 0) PUSH(*out, (uint32_t) best);
    }
}

/* Every root that reaches t, by walking callers backwards over live defs. */
static void all_roots(uint32_t t, u32vec *roots) {
    csr_t callers;
    callers.off = xcalloc((size_t) NN * 2 + 2, sizeof(uint32_t));
    for (size_t d = 0; d < D.n; d++)
        for (size_t k = 0; k < D.a[d].refs.n; k++) {
            const ref_t *r = &D.a[d].refs.a[k];
            if (r->name < NN) callers.off[r->name * 2 + r->is_const + 2]++;
        }
    for (size_t k = 0; k < (size_t) NN * 2; k++) callers.off[k + 2] += callers.off[k + 1];
    callers.items = xcalloc(callers.off[(size_t) NN * 2 + 1] + 1, sizeof(uint32_t));
    for (size_t d = 0; d < D.n; d++)
        for (size_t k = 0; k < D.a[d].refs.n; k++) {
            const ref_t *r = &D.a[d].refs.a[k];
            if (r->name < NN) callers.items[callers.off[r->name * 2 + r->is_const + 1]++] = (uint32_t) d;
        }
    uint8_t *seen = xcalloc(D.n + 1, 1);
    u32vec st = {0};
    PUSH(st, t);
    seen[t] = 1;
    while (st.n) {
        uint32_t u = st.a[--st.n];
        if (D.a[u].root_rule != NOT_ROOT) PUSH(*roots, u);
        const def_t *d = &D.a[u];
        if (d->kind == K_LOAD || d->name >= NN) continue;
        uint32_t key = def_key(d);
        for (uint32_t k = callers.off[key]; k < callers.off[key + 1]; k++) {
            uint32_t c = callers.items[k];
            if (!seen[c] && D.a[c].live) {
                seen[c] = 1;
                PUSH(st, c);
            }
        }
        if (d->via == VIA_HOLDS && d->parent >= 0 && !seen[d->parent]) { /* namespaces held alive */
            seen[d->parent] = 1;
            PUSH(st, (uint32_t) d->parent);
        }
    }
    free(st.a);
    free(seen);
    free(callers.off);
    free(callers.items);
    qsort(roots->a, roots->n, sizeof *roots->a, def_loc_qcmp);
}

static void explain_one(uint32_t t, int json, int first) {
    const def_t *d = &D.a[t];
    u32vec path = {0};
    if (d->live)
        for (int32_t u = (int32_t) t; u >= 0; u = D.a[u].parent) {
            PUSH(path, (uint32_t) u);
            if (path.n > D.n) break;
        }
    if (json) {
        FILE *f = OUT;
        fputs(first ? "{" : ",\n{", f);
        fputs(" \"id\": ", f);
        json_id(f, t);
        fprintf(f, ", \"live\": %s, \"reason\": \"%s\", \"location\": ", d->live ? "true" : "false", reason_word(d->reason));
        sbuf loc = {0};
        sb_printf(&loc, "%s:%u", FILES.a[d->file], d->line);
        json_str(f, loc.s);
        free(loc.s);
        fputs(", \"path\": [", f);
        for (size_t k = path.n; k-- > 0;) {
            uint32_t u = path.a[k];
            const def_t *x = &D.a[u];
            fputs(k == path.n - 1 ? "{ \"id\": " : ", { \"id\": ", f);
            json_id(f, u);
            sbuf via = {0}, l = {0};
            if (k == path.n - 1) {
                sb_puts(&via, "root: ");
                rule_desc(x->root_rule, &via);
            } else {
                sb_puts(&via, x->via == VIA_HOLDS ? "holds" : "calls");
            }
            fputs(", \"via\": ", f);
            json_str(f, via.s);
            if (k == path.n - 1) {
                if (x->kind == K_LOAD) sb_puts(&l, "");
                else sb_printf(&l, "%s:%u", FILES.a[x->file], x->line);
            } else {
                sb_printf(&l, "%s:%u", FILES.a[x->pfile], x->pline);
            }
            fputs(", \"location\": ", f);
            json_str(f, l.s);
            fputs(" }", f);
            free(via.s);
            free(l.s);
        }
        fputs("]", f);
        if (!d->live) {
            u32vec cs = {0};
            dead_callers(t, &cs);
            fputs(", \"callers\": [", f);
            for (size_t c = 0; c < cs.n; c++) {
                if (c) fputs(", ", f);
                json_id(f, cs.a[c]);
            }
            fprintf(f, "], \"batch\": %d", d->batch + 1);
            free(cs.a);
        }
        if (O.all_paths && d->live) {
            u32vec roots = {0};
            all_roots(t, &roots);
            fputs(", \"roots\": [", f);
            for (size_t k = 0; k < roots.n; k++) {
                if (k) fputs(", ", f);
                json_id(f, roots.a[k]);
            }
            fputs("]", f);
            free(roots.a);
        }
        fputs(" }", f);
        free(path.a);
        return;
    }
    if (!first) fputc('\n', OUT);
    if (d->live) {
        fprintf(OUT, "%s%s%s is live  %s(%s:%u)%s\n", C_BOLD, S(d->qual), C_OFF, C_DIM, FILES.a[d->file], d->line, C_OFF);
        int w = 0;
        for (size_t k = 0; k < path.n; k++) {
            int l = (int) SLEN(D.a[path.a[k]].qual);
            if (l > w) w = l;
        }
        for (size_t k = path.n; k-- > 0;) {
            const def_t *x = &D.a[path.a[k]];
            sbuf rest = {0};
            if (k == path.n - 1) rule_desc(x->root_rule, &rest);
            else sb_printf(&rest, "%s:%u", FILES.a[x->pfile], x->pline);
            const char *verb = k == path.n - 1 ? "root " : x->via == VIA_HOLDS ? "holds" : "calls";
            if (k != path.n - 1 && x->via == VIA_HOLDS) verb = "in   ";
            fprintf(OUT, "  %s  %-*s  %s%s%s\n", verb, w, S(x->qual), C_DIM, rest.s ? rest.s : "", C_OFF);
            free(rest.s);
        }
        if (O.all_paths) {
            u32vec roots = {0};
            all_roots(t, &roots);
            fprintf(OUT, "  %zu root%s reach it:\n", roots.n, roots.n == 1 ? "" : "s");
            for (size_t k = 0; k < roots.n; k++) {
                const def_t *x = &D.a[roots.a[k]];
                sbuf rest = {0};
                rule_desc(x->root_rule, &rest);
                fprintf(OUT, "    %-*s  %s%s%s\n", w, S(x->qual), C_DIM, rest.s ? rest.s : "", C_OFF);
                free(rest.s);
            }
            free(roots.a);
        }
    } else {
        static const char *why[] = {NULL, "nothing names it", "only dead code reaches it",
                                    "it is reached only from code that calls itself"};
        fprintf(OUT, "%s%s%s is dead: %s%s%s (%s)\n", C_BOLD, S(d->qual), C_OFF, C_RED, reason_word(d->reason), C_OFF, why[d->reason]);
        fprintf(OUT, "  defined   %s:%u-%u  %s %s\n", FILES.a[d->file], d->line, d->end_line, vis_word(d->vis), kind_word(d->kind));
        u32vec cs = {0};
        dead_callers(t, &cs);
        for (size_t c = 0; c < cs.n; c++) {
            const def_t *x = &D.a[cs.a[c]];
            const ref_t *r = find_ref(x, d->name, is_constk(d->kind));
            fprintf(OUT, "  called by %s  %s%s:%u (dead)%s\n", S(x->qual), C_DIM, r ? FILES.a[r->file] : FILES.a[x->file],
                    r ? r->line : x->line, C_OFF);
        }
        free(cs.a);
        u32vec tf = {0};
        test_files_of(t, &tf);
        if (tf.n) {
            fputs("  tests     ", OUT);
            for (size_t k = 0; k < tf.n; k++) fprintf(OUT, "%s%s", k ? ", " : "", FILES.a[tf.a[k]]);
            fputs("  (tests never keep code alive)\n", OUT);
        }
        free(tf.a);
        if (d->batch >= 0) {
            const batch_t *B = &BATCHES.a[d->batch];
            fprintf(OUT, "  batch %d   %zu definition%s, %u lines:", d->batch + 1, B->members.n, B->members.n == 1 ? "" : "s", B->lines);
            for (size_t m = 0; m < B->members.n && m < 6; m++) fprintf(OUT, "%s %s", m ? "," : "", S(D.a[B->members.a[m]].qual));
            if (B->members.n > 6) fprintf(OUT, " and %zu more", B->members.n - 6);
            fputc('\n', OUT);
        }
    }
    free(path.a);
}

int cmd_explain(void) {
    u32vec hits = {0};
    find_defs(O.explain_name, &hits);
    if (!hits.n) {
        fprintf(stderr, "backburn: no definition named %s (try a qualified name like Order#total, Order.find, Admin::Report, or file:line)\n",
                O.explain_name);
        return 2;
    }
    int json = O.format == F_JSON;
    if (json && hits.n > 1) fputs("[\n", OUT);
    for (size_t k = 0; k < hits.n; k++) explain_one(hits.a[k], json, k == 0);
    if (json) fputs(hits.n > 1 ? "\n]\n" : "\n", OUT);
    free(hits.a);
    return 0;
}
