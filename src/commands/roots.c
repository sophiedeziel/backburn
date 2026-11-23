/* roots: how many definitions each root rule matched. */
#include <fnmatch.h>

#include "commands/commands.h"
#include "core/json.h"
#include "cli/options.h"
#include "analysis/graph.h"
#include "analysis/roots.h"
#include "output/report.h"

int cmd_roots(void) {
    uint32_t builtin[7] = {0};
    for (uint32_t i = 0; i < D.n; i++) {
        const def_t *d = &D.a[i];
        if (d->kind == K_LOAD) {
            builtin[-RR_LOAD]++;
            continue;
        }
        int32_t pr = FILE_PATH_RULE[d->file];
        if (pr >= 0)
            for (size_t r = 0; r < RULES.n; r++)
                if (RULES.a[r].type == RULE_PATH && !fnmatch(RULES.a[r].pat, FILES.a[d->file], 0)) RULES.a[r].matched++;
        if (is_meth(d->kind)) {
            if (d->overrides) builtin[-RR_SUPER]++;
            if (is_operator(d->name)) builtin[-RR_OPERATOR]++;
            if (d->name < NN && IS_HOOK[d->name]) builtin[-RR_HOOK]++;
            if (O.public_api && d->vis == V_PUBLIC && !d->nodoc) builtin[-RR_PUBLIC_API]++;
        } else if (O.public_api && !d->nodoc) {
            builtin[-RR_PUBLIC_API]++;
        }
        for (size_t r = 0; r < RULES.n; r++)
            if (RULES.a[r].type != RULE_PATH && rule_matches(&RULES.a[r], i)) RULES.a[r].matched++;
    }
    if (O.format == F_TSV) {
        fputs("rule\tsource\tmatched\n", OUT);
        for (size_t r = 0; r < RULES.n; r++)
            if (!O.unused_only || !RULES.a[r].matched)
                fprintf(OUT, "%s\t%s\t%u\n", RULES.a[r].text, RULES.a[r].source, RULES.a[r].matched);
        if (!O.unused_only) {
            fprintf(OUT, "<load time>\tbuilt-in\t%u\ncalls super\tbuilt-in\t%u\noperators\tbuilt-in\t%u\nhooks\tbuilt-in\t%u\n",
                    builtin[2], builtin[3], builtin[4], builtin[5]);
            if (O.public_api) fprintf(OUT, "--public-api\tbuilt-in\t%u\n", builtin[6]);
        }
        return 0;
    }
    if (O.format == F_JSON) {
        fputs("{\n  \"schema\": 1,\n  \"rules\": [", OUT);
        int first = 1;
        for (size_t r = 0; r < RULES.n; r++) {
            if (O.unused_only && RULES.a[r].matched) continue;
            fputs(first ? "\n    { \"rule\": " : ",\n    { \"rule\": ", OUT);
            first = 0;
            json_str(OUT, RULES.a[r].text);
            fputs(", \"source\": ", OUT);
            json_str(OUT, RULES.a[r].source);
            fprintf(OUT, ", \"matched\": %u }", RULES.a[r].matched);
        }
        fprintf(OUT, "%s,\n  \"builtin\": { \"load_time\": %u, \"calls_super\": %u, \"operators\": %u, \"hooks\": %u, \"public_api\": %u }\n}\n",
                first ? "]" : "\n  ]", builtin[2], builtin[3], builtin[4], builtin[5], builtin[6]);
        return 0;
    }
    header_line();
    int w = 0;
    for (size_t r = 0; r < RULES.n; r++)
        if ((int) strlen(RULES.a[r].text) > w) w = (int) strlen(RULES.a[r].text);
    size_t unused = 0;
    for (size_t r = 0; r < RULES.n; r++) unused += !RULES.a[r].matched;
    fprintf(OUT, "%zu rules · %zu matched nothing\n\n", RULES.n, unused);
    for (size_t r = 0; r < RULES.n; r++) {
        const rule_t *R = &RULES.a[r];
        if (O.unused_only && R->matched) continue;
        fprintf(OUT, "  %6u  %-*s  %s%s%s%s\n", R->matched, w, R->text, C_DIM, R->source, C_OFF,
                R->matched ? "" : "  (matched nothing)");
    }
    if (!O.unused_only) {
        fprintf(OUT, "\n%sBuilt in%s\n", C_BOLD, C_OFF);
        fprintf(OUT, "  %6u  <load time>\n  %6u  methods that call super\n  %6u  operators\n  %6u  Ruby hooks\n", builtin[2],
                builtin[3], builtin[4], builtin[5]);
        if (O.public_api) fprintf(OUT, "  %6u  public, documented API (--public-api)\n", builtin[6]);
    }
    return 0;
}
