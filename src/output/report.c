/* Reports for scan and batches: text, tsv, json, sarif (and the legacy tsv). */

#include "output/report.h"
#include "core/intern.h"
#include "core/model.h"
#include "core/json.h"
#include "cli/options.h"
#include "analysis/mark.h"
#include "analysis/analysis.h"
#include "output/baseline.h"

const char *reason_word(int r) {
    return r == R_NEVER ? "unreferenced" : r == R_DEAD_ONLY ? "dead-chain" : r == R_CYCLE ? "dead-cycle" : "live";
}

static const char *legacy_reason(int r) {
    return r == R_NEVER ? "never_referenced" : r == R_DEAD_ONLY ? "only_dead_callers" : "dead_cycle";
}

const char *vis_word(int v) { return v == V_PRIVATE ? "private" : v == V_PROTECTED ? "protected" : "public"; }

void json_id(FILE *f, uint32_t i) {
    sbuf b = {0};
    def_id(&b, i);
    json_str(f, b.s);
    free(b.s);
}

/* The candidate set after --only and --test-refs. */
u32vec CANDS;

void select_candidates(void) {
    for (uint32_t i = 1; i < D.n; i++) {
        const def_t *d = &D.a[i];
        if (d->live) continue;
        if (O.only_mask && !(O.only_mask & (1 << d->reason))) continue;
        if (O.test_refs == TR_EXCLUDE && d->test_ref) continue;
        if (O.test_refs == TR_ONLY && !d->test_ref) continue;
        PUSH(CANDS, i);
    }
    qsort(CANDS.a, CANDS.n, sizeof *CANDS.a, def_loc_qcmp);
}

int batch_selected(const batch_t *b) {
    if (O.untested && b->test_files.n) return 0;
    if (O.max_lines > 0 && b->lines > (uint32_t) O.max_lines) return 0;
    return 1;
}

void header_line(void) {
    if (O.quiet) return;
    fprintf(OUT, "%sbackburn %s%s · %zu files · %zu definitions · %.0f ms", C_BOLD, BACKBURN_VERSION, C_OFF,
            FILES.n, D.n - 1, ELAPSED_MS);
    if (O.presets.n) {
        fputs(" · presets: ", OUT);
        for (size_t i = 0; i < O.presets.n; i++) fprintf(OUT, "%s%s", i ? ", " : "", O.presets.a[i]);
    }
    if (O.roots.n) {
        fputs(" · roots: ", OUT);
        for (size_t i = 0; i < O.roots.n; i++) {
            const char *r = O.roots.a[i];
            size_t rl = strlen(ROOT_ABS);
            size_t ol = strlen(O.root);
            if (!strncmp(r, ROOT_ABS, rl) && r[rl] == '/') r += rl + 1; /* config roots, relative to PATH */
            else if (!strncmp(r, O.root, ol) && r[ol] == '/') r += ol + 1;
            else if (!strncmp(r, "./", 2)) r += 2;
            fprintf(OUT, "%s%s", i ? ", " : "", r);
        }
    }
    if (O.public_api) fputs(" · public API", OUT);
    fputc('\n', OUT);
}

static void print_parse_errors(void) {
    if (TEMPLATE_ERRORS && !O.quiet)
        fprintf(OUT, "\n%s%u view template%s had Ruby backburn could only partly read; references in them are best-effort%s\n",
                C_DIM, TEMPLATE_ERRORS, TEMPLATE_ERRORS == 1 ? "" : "s", C_OFF);
    if (!PERR.n) return;
    fprintf(OUT, "\n%sCould not parse (%zu files)%s — the report may miss callers in them\n", C_BOLD, PERR.n, C_OFF);
    for (size_t i = 0; i < PERR.n; i++)
        fprintf(OUT, "  %s:%u  %s\n", FILES.a[PERR.a[i].file], PERR.a[i].line, PERR.a[i].msg);
}

void text_scan(void) {
    size_t cnt[4] = {0}, tests = 0;
    for (size_t k = 0; k < CANDS.n; k++) {
        cnt[D.a[CANDS.a[k]].reason]++;
        tests += D.a[CANDS.a[k]].test_ref;
    }
    header_line();
    if (!O.quiet)
        fprintf(OUT, "%zu dead · %zu unreferenced · %zu dead-chain · %zu dead-cycle · %zu used only by tests\n",
                CANDS.n, cnt[R_NEVER], cnt[R_DEAD_ONLY], cnt[R_CYCLE], tests);
    int width = 0;
    for (size_t k = 0; k < CANDS.n; k++) {
        const def_t *d = &D.a[CANDS.a[k]];
        int w = (int) strlen(FILES.a[d->file]) + 1 + snprintf(NULL, 0, "%u", d->line);
        if (w > width) width = w;
    }
    if (width > 48) width = 48;
    static const char *titles[] = {NULL, "Unreferenced", "Dead chain: only dead code reaches it",
                                   "Dead cycle: reached only from code that calls itself"};
    for (int r = R_NEVER; r <= R_CYCLE; r++) {
        if (!cnt[r]) continue;
        fprintf(OUT, "\n%s%s (%zu)%s\n", C_BOLD, titles[r], cnt[r], C_OFF);
        for (size_t k = 0; k < CANDS.n; k++) {
            uint32_t i = CANDS.a[k];
            const def_t *d = &D.a[i];
            if (d->reason != r) continue;
            char loc[1024];
            snprintf(loc, sizeof loc, "%s:%u", FILES.a[d->file], d->line);
            fprintf(OUT, "  %-*s  %-8s %s", width, loc, kind_word(d->kind), S(d->qual));
            if (HAVE_BASELINE) {
                sbuf id = {0};
                def_id(&id, i);
                if (!in_baseline(id.s)) fprintf(OUT, "  %snew%s", C_RED, C_OFF);
                free(id.s);
            }
            if (d->test_ref) fprintf(OUT, "  %s[tests]%s", C_YEL, C_OFF);
            if (r != R_NEVER) {
                u32vec cs = {0};
                dead_callers(i, &cs);
                fprintf(OUT, "  %s←", C_DIM);
                for (size_t c = 0; c < cs.n && c < 3; c++) fprintf(OUT, "%s %s", c ? "," : "", S(D.a[cs.a[c]].qual));
                if (cs.n > 3) fprintf(OUT, " and %zu more", cs.n - 3);
                fputs(C_OFF, OUT);
                free(cs.a);
            }
            fputc('\n', OUT);
        }
    }
    if (DYN.n) {
        fprintf(OUT, "\n%sDynamic dispatch that may hide callers (%zu)%s\n", C_BOLD, DYN.n, C_OFF);
        size_t shown = DYN.n < 15 ? DYN.n : 15;
        for (size_t k = 0; k < shown; k++) {
            char loc[1024];
            snprintf(loc, sizeof loc, "%s:%u", FILES.a[DYN.a[k].file], DYN.a[k].line);
            fprintf(OUT, "  %-*s  %s(<computed>)\n", width > 0 ? width : 36, loc, DYN.a[k].what);
        }
        if (DYN.n > shown) fprintf(OUT, "  … and %zu more\n", DYN.n - shown);
    }
    print_parse_errors();
}

void text_batches(void) {
    size_t nb = 0, lines = 0, untested = 0;
    for (size_t b = 0; b < BATCHES.n; b++) {
        if (!batch_selected(&BATCHES.a[b])) continue;
        nb++;
        lines += BATCHES.a[b].lines;
        untested += !BATCHES.a[b].test_files.n;
    }
    header_line();
    if (!O.quiet)
        fprintf(OUT, "%zu removal batches · %zu lines in all · %zu have no test references\n", nb, lines, untested);
    for (size_t b = 0; b < BATCHES.n; b++) {
        const batch_t *B = &BATCHES.a[b];
        if (!batch_selected(B)) continue;
        fprintf(OUT, "\n%sBatch %zu · %zu definition%s · %u line%s · %u file%s · ", C_BOLD, b + 1, B->members.n,
                B->members.n == 1 ? "" : "s", B->lines, B->lines == 1 ? "" : "s", B->nfiles, B->nfiles == 1 ? "" : "s");
        if (!B->test_files.n) fputs("no test references", OUT);
        else {
            fputs("tests: ", OUT);
            for (size_t t = 0; t < B->test_files.n && t < 4; t++) fprintf(OUT, "%s%s", t ? ", " : "", FILES.a[B->test_files.a[t]]);
            if (B->test_files.n > 4) fprintf(OUT, " and %zu more", B->test_files.n - 4);
        }
        fprintf(OUT, "%s\n", C_OFF);
        for (size_t m = 0; m < B->members.n; m++) {
            const def_t *d = &D.a[B->members.a[m]];
            char loc[1024];
            snprintf(loc, sizeof loc, "%s:%u-%u", FILES.a[d->file], d->line, d->end_line);
            fprintf(OUT, "  %-52s %-8s %s", loc, kind_word(d->kind), S(d->qual));
            if (d->reason != R_NEVER) fprintf(OUT, "  %s(reached only from this batch)%s", C_DIM, C_OFF);
            if (d->test_ref) fprintf(OUT, "  %s[tests]%s", C_YEL, C_OFF);
            fputc('\n', OUT);
        }
    }
    print_parse_errors();
}

static void tsv_row(uint32_t i) {
    const def_t *d = &D.a[i];
    sbuf id = {0};
    def_id(&id, i);
    fprintf(OUT, "%s\t%s\t%s\t%s\t%u\t%u\t%s\t%d\t%d\n", id.s, kind_word(d->kind), S(d->qual), FILES.a[d->file], d->line,
            d->end_line, reason_word(d->reason), d->test_ref, d->batch + 1);
    free(id.s);
}

void tsv_scan(void) {
    fputs("id\tkind\tname\tfile\tline\tend_line\treason\ttests\tbatch\n", OUT);
    for (size_t k = 0; k < CANDS.n; k++) tsv_row(CANDS.a[k]);
}

static int batch_tsv_cmp(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *) a, y = *(const uint32_t *) b;
    if (D.a[x].batch != D.a[y].batch) return D.a[x].batch < D.a[y].batch ? -1 : 1;
    if (D.a[x].reason != D.a[y].reason) return D.a[x].reason < D.a[y].reason ? -1 : 1;
    return def_loc_cmp(x, y);
}

void tsv_batches(void) {
    fputs("id\tkind\tname\tfile\tline\tend_line\treason\ttests\tbatch\n", OUT);
    u32vec rows = {0};
    for (size_t k = 0; k < CANDS.n; k++)
        if (batch_selected(&BATCHES.a[D.a[CANDS.a[k]].batch])) PUSH(rows, CANDS.a[k]);
    qsort(rows.a, rows.n, sizeof *rows.a, batch_tsv_cmp);
    for (size_t k = 0; k < rows.n; k++) tsv_row(rows.a[k]);
    free(rows.a);
}

void legacy_tsv_scan(void) {
    fputs("file\tline\tkind\tname\treason\tonly_in_tests\n", OUT);
    for (size_t k = 0; k < CANDS.n; k++) {
        const def_t *d = &D.a[CANDS.a[k]];
        fprintf(OUT, "%s\t%u\t%s\t%s\t%s\t%d\n", FILES.a[d->file], d->line, kind_word(d->kind), S(d->qual),
                legacy_reason(d->reason), d->test_ref);
    }
}

void legacy_tsv_batches(void) {
    fputs("batch\tfile\tline\tend_line\tkind\tname\treason\tonly_in_tests\n", OUT);
    for (size_t b = 0; b < BATCHES.n; b++)
        for (size_t m = 0; m < BATCHES.a[b].members.n; m++) {
            const def_t *d = &D.a[BATCHES.a[b].members.a[m]];
            fprintf(OUT, "%zu\t%s\t%u\t%u\t%s\t%s\t%s\t%d\n", b + 1, FILES.a[d->file], d->line, d->end_line,
                    kind_word(d->kind), S(d->qual), legacy_reason(d->reason), d->test_ref);
        }
}

static void json_presets_array(FILE *f) {
    fputc('[', f);
    for (size_t i = 0; i < O.presets.n; i++) {
        if (i) fputs(", ", f);
        json_str(f, O.presets.a[i]);
    }
    fputc(']', f);
}

void json_report(void) {
    FILE *f = OUT;
    fprintf(f, "{\n  \"schema\": 1,\n  \"tool\": { \"name\": \"backburn\", \"version\": \"%s\" },\n", BACKBURN_VERSION);
    fputs("  \"project\": { \"root\": ", f);
    json_str(f, ROOT_ABS);
    fputs(", \"presets\": ", f);
    json_presets_array(f);
    fputs(", \"roots\": [", f);
    for (size_t i = 0; i < O.roots.n; i++) {
        if (i) fputs(", ", f);
        json_str(f, O.roots.a[i]);
    }
    fputs("], \"git\": ", f);
    char *g = git_head();
    if (g) json_str(f, g);
    else fputs("null", f);
    fprintf(f, " },\n  \"stats\": { \"files\": %zu, \"definitions\": %zu, \"references\": %zu, \"test_references\": %zu, \"ms\": %.0f },\n",
            FILES.n, D.n - 1, NREFS, TR.n, ELAPSED_MS);
    fputs("  \"candidates\": [", f);
    for (size_t k = 0; k < CANDS.n; k++) {
        uint32_t i = CANDS.a[k];
        const def_t *d = &D.a[i];
        fputs(k ? ",\n    {" : "\n    {", f);
        fputs(" \"id\": ", f);
        json_id(f, i);
        fprintf(f, ", \"kind\": \"%s\", \"name\": ", kind_word(d->kind));
        json_str(f, S(d->qual));
        fputs(", \"location\": { \"file\": ", f);
        json_str(f, FILES.a[d->file]);
        fprintf(f, ", \"line\": %u, \"end_line\": %u }, \"visibility\": \"%s\", \"reason\": \"%s\", \"tests\": %s",
                d->line, d->end_line, vis_word(d->vis), reason_word(d->reason), d->test_ref ? "true" : "false");
        u32vec tf = {0}, cs = {0};
        test_files_of(i, &tf);
        fputs(", \"test_files\": [", f);
        for (size_t t = 0; t < tf.n; t++) {
            if (t) fputs(", ", f);
            json_str(f, FILES.a[tf.a[t]]);
        }
        dead_callers(i, &cs);
        fputs("], \"callers\": [", f);
        for (size_t c = 0; c < cs.n; c++) {
            if (c) fputs(", ", f);
            json_id(f, cs.a[c]);
        }
        fprintf(f, "], \"batch\": %d", d->batch + 1);
        if (HAVE_BASELINE) {
            sbuf id = {0};
            def_id(&id, i);
            fprintf(f, ", \"new\": %s", in_baseline(id.s) ? "false" : "true");
            free(id.s);
        }
        fputs(" }", f);
        free(tf.a);
        free(cs.a);
    }
    fputs(CANDS.n ? "\n  ],\n" : "],\n", f);
    fputs("  \"batches\": [", f);
    int first = 1;
    for (size_t b = 0; b < BATCHES.n; b++) {
        const batch_t *B = &BATCHES.a[b];
        if (!batch_selected(B)) continue;
        fprintf(f, "%s{ \"id\": %zu, \"lines\": %u, \"files\": [", first ? "\n    " : ",\n    ", b + 1, B->lines);
        first = 0;
        u32vec fl = {0};
        for (size_t m = 0; m < B->members.n; m++) PUSH(fl, D.a[B->members.a[m]].file);
        qsort(fl.a, fl.n, sizeof *fl.a, u32_cmp);
        int fw = 0;
        for (size_t m = 0; m < fl.n; m++) {
            if (m && fl.a[m] == fl.a[m - 1]) continue;
            if (fw++) fputs(", ", f);
            json_str(f, FILES.a[fl.a[m]]);
        }
        free(fl.a);
        fputs("], \"definitions\": [", f);
        for (size_t m = 0; m < B->members.n; m++) {
            if (m) fputs(", ", f);
            json_id(f, B->members.a[m]);
        }
        fputs("], \"test_files\": [", f);
        for (size_t t = 0; t < B->test_files.n; t++) {
            if (t) fputs(", ", f);
            json_str(f, FILES.a[B->test_files.a[t]]);
        }
        fputs("] }", f);
    }
    fputs(first ? "],\n" : "\n  ],\n", f);
    fputs("  \"dynamic_sites\": [", f);
    for (size_t k = 0; k < DYN.n; k++) {
        fputs(k ? ",\n    { \"file\": " : "\n    { \"file\": ", f);
        json_str(f, FILES.a[DYN.a[k].file]);
        fprintf(f, ", \"line\": %u, \"call\": ", DYN.a[k].line);
        json_str(f, DYN.a[k].what);
        fputs(" }", f);
    }
    fputs(DYN.n ? "\n  ],\n" : "],\n", f);
    fputs("  \"unparsed\": [", f);
    for (size_t k = 0; k < PERR.n; k++) {
        fputs(k ? ", { \"file\": " : "{ \"file\": ", f);
        json_str(f, FILES.a[PERR.a[k].file]);
        fprintf(f, ", \"line\": %u, \"message\": ", PERR.a[k].line);
        json_str(f, PERR.a[k].msg);
        fputs(" }", f);
    }
    fputs("]\n}\n", f);
}

void sarif_report(void) {
    FILE *f = OUT;
    static const char *ids[] = {NULL, "backburn/unreferenced", "backburn/dead-chain", "backburn/dead-cycle"};
    static const char *desc[] = {NULL, "Nothing names this definition.", "Only dead code reaches this definition.",
                                 "This definition is reached only from a group of dead code that calls itself."};
    fputs("{\n  \"$schema\": \"https://json.schemastore.org/sarif-2.1.0.json\",\n  \"version\": \"2.1.0\",\n  \"runs\": [{\n", f);
    fprintf(f, "    \"tool\": { \"driver\": { \"name\": \"backburn\", \"version\": \"%s\", \"rules\": [", BACKBURN_VERSION);
    for (int r = 1; r <= 3; r++)
        fprintf(f, "%s\n      { \"id\": \"%s\", \"shortDescription\": { \"text\": \"%s\" }, \"defaultConfiguration\": { \"level\": \"note\" } }",
                r > 1 ? "," : "", ids[r], desc[r]);
    fputs("\n    ] } },\n", f);
    fputs("    \"originalUriBaseIds\": { \"%SRCROOT%\": { \"uri\": ", f);
    sbuf u = {0};
    sb_puts(&u, "file://");
    sb_puts(&u, ROOT_ABS);
    if (u.s[u.n - 1] != '/') sb_putc(&u, '/');
    json_str(f, u.s);
    free(u.s);
    fputs(" } },\n    \"results\": [", f);
    for (size_t k = 0; k < CANDS.n; k++) {
        uint32_t i = CANDS.a[k];
        const def_t *d = &D.a[i];
        sbuf msg = {0};
        sb_printf(&msg, "%s %s is dead: %s", kind_word(d->kind), S(d->qual),
                  d->reason == R_NEVER ? "nothing names it" : d->reason == R_DEAD_ONLY ? "only dead code reaches it"
                                                                                       : "it is reached only from a dead cycle");
        if (d->test_ref) sb_puts(&msg, " (tests name it, but tests never keep code alive)");
        fprintf(f, "%s\n      { \"ruleId\": \"%s\", \"ruleIndex\": %d, \"level\": \"note\", \"message\": { \"text\": ",
                k ? "," : "", ids[d->reason], d->reason - 1);
        json_str(f, msg.s);
        free(msg.s);
        fputs(" }, \"locations\": [{ \"physicalLocation\": { \"artifactLocation\": { \"uri\": ", f);
        json_str(f, FILES.a[d->file]);
        fprintf(f, ", \"uriBaseId\": \"%%SRCROOT%%\" }, \"region\": { \"startLine\": %u, \"endLine\": %u } } }], ", d->line,
                d->end_line < d->line ? d->line : d->end_line);
        fputs("\"partialFingerprints\": { \"backburnId\": ", f);
        json_id(f, i);
        fprintf(f, " }, \"properties\": { \"batch\": %d, \"tests\": %s } }", d->batch + 1, d->test_ref ? "true" : "false");
    }
    fputs(CANDS.n ? "\n    ]\n  }]\n}\n" : "]\n  }]\n}\n", f);
}
