/* scan and batches: print the report, then apply the baseline and --fail-on to pick the exit code. */
#include <unistd.h>

#include "commands/commands.h"
#include "core/model.h"
#include "cli/options.h"
#include "analysis/analysis.h"
#include "analysis/batches.h"
#include "output/baseline.h"
#include "output/report.h"

/* A baseline at the default path applies by itself; --baseline or the config only move it. */
static void load_baseline(void) {
    if (!O.baseline) {
        char *def = root_path(".backburn-baseline.json");
        if (access(def, R_OK) == 0) O.baseline = def;
        else free(def);
    }
    if (O.baseline) {
        if (!read_baseline(O.baseline))
            fprintf(stderr, "backburn: baseline %s not found; every candidate counts as new (write one with --write-baseline)\n",
                    O.baseline);
        HAVE_BASELINE = 1;
    }
    if (O.fail_on == FAIL_UNSET) O.fail_on = O.baseline ? FAIL_NEW : FAIL_NONE;
}

static void print_report(void) {
    int batches = O.cmd == CMD_BATCHES;
    switch (O.format) {
    case F_LEGACY_TSV: batches ? legacy_tsv_batches() : legacy_tsv_scan(); break;
    case F_TSV: batches ? tsv_batches() : tsv_scan(); break;
    case F_JSON: json_report(); break;
    case F_SARIF: sarif_report(); break;
    default: batches ? text_batches() : text_scan(); break;
    }
}

/* Baseline entries that are no longer dead (or no longer exist): hint that it can shrink. */
static void warn_stale_baseline(void) {
    strvec cur = {0};
    for (uint32_t i = 1; i < D.n; i++)
        if (!D.a[i].live) {
            sbuf b = {0};
            def_id(&b, i);
            PUSH(cur, b.s);
        }
    qsort(cur.a, cur.n, sizeof *cur.a, cmp_cstr);
    size_t stale = 0;
    for (size_t k = 0; k < BASELINE.n; k++)
        if (!bsearch(&BASELINE.a[k], cur.a, cur.n, sizeof *cur.a, cmp_cstr)) stale++;
    for (size_t k = 0; k < cur.n; k++) free(cur.a[k]);
    free(cur.a);
    if (stale)
        fprintf(stderr, "backburn: %zu baseline entr%s no longer dead or gone; run `backburn scan --write-baseline` to shrink it\n",
                stale, stale == 1 ? "y is" : "ies are");
}

static size_t count_failing(void) {
    size_t failing = 0;
    for (size_t k = 0; k < CANDS.n; k++) {
        if (O.fail_on == FAIL_ANY) failing++;
        else if (O.fail_on == FAIL_NEW) {
            sbuf b = {0};
            def_id(&b, CANDS.a[k]);
            failing += !in_baseline(b.s);
            free(b.s);
        }
    }
    return failing;
}

int cmd_scan(void) {
    if (O.write_baseline) return write_baseline_file();
    load_baseline();
    select_candidates();
    if (O.cmd == CMD_BATCHES && (O.untested || O.max_lines)) {
        size_t w = 0;
        for (size_t k = 0; k < CANDS.n; k++)
            if (batch_selected(&BATCHES.a[D.a[CANDS.a[k]].batch])) CANDS.a[w++] = CANDS.a[k];
        CANDS.n = w;
    }
    print_report();
    if (HAVE_BASELINE) warn_stale_baseline();
    size_t failing = count_failing();
    if (PERR.n && O.format != F_TEXT)
        fprintf(stderr, "backburn: %zu Ruby file%s could not be parsed\n", PERR.n, PERR.n == 1 ? "" : "s");
    if (failing) return 1;
    if (PERR.n) return 3;
    return 0;
}
