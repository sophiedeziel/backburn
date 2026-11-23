/* Baselines: accept today's dead code so CI only fails on new dead code. */
#include <errno.h>

#include "output/baseline.h"
#include "core/model.h"
#include "core/json.h"
#include "cli/options.h"
#include "analysis/analysis.h"

strvec BASELINE;

int HAVE_BASELINE;

char *git_head(void) {
    static char *cached;
    static int done;
    if (done) return cached;
    done = 1;
    sbuf cmd = {0};
    sb_puts(&cmd, "git -C ");
    sh_quote(&cmd, O.root);
    sb_puts(&cmd, " rev-parse --short HEAD 2>/dev/null");
    FILE *p = popen(cmd.s, "r");
    free(cmd.s);
    if (!p) return NULL;
    char buf[64] = {0};
    if (fgets(buf, sizeof buf, p)) {
        buf[strcspn(buf, "\r\n")] = 0;
        if (*buf) cached = xstrdup(buf);
    }
    pclose(p);
    return cached;
}

/* Reads the "ids" array of a baseline file: just the JSON strings in it. */
int read_baseline(const char *path) {
    size_t n;
    char *t = read_file(path, &n);
    if (!t) return 0;
    char *p = strstr(t, "\"ids\"");
    p = p ? strchr(p, '[') : NULL;
    while (p && *p && *p != ']') {
        if (*p == '"') {
            sbuf s = {0};
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) {
                    p++;
                    char c = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p;
                    sb_putc(&s, c);
                } else {
                    sb_putc(&s, *p);
                }
                p++;
            }
            PUSH(BASELINE, s.s ? s.s : xstrdup(""));
        }
        if (*p) p++;
    }
    free(t);
    qsort(BASELINE.a, BASELINE.n, sizeof *BASELINE.a, cmp_cstr);
    return 1;
}

int in_baseline(const char *id) {
    return bsearch(&id, BASELINE.a, BASELINE.n, sizeof *BASELINE.a, cmp_cstr) != NULL;
}

int write_baseline_file(void) {
    const char *path = O.baseline ? O.baseline : root_path(".backburn-baseline.json");
    strvec ids = {0};
    for (uint32_t i = 1; i < D.n; i++)
        if (!D.a[i].live) {
            sbuf b = {0};
            def_id(&b, i);
            PUSH(ids, b.s);
        }
    qsort(ids.a, ids.n, sizeof *ids.a, cmp_cstr);
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "backburn: cannot write %s: %s\n", path, strerror(errno));
        return 2;
    }
    fputs("{\n  \"schema\": 1,\n  \"commit\": ", f);
    char *g = git_head();
    if (g) json_str(f, g);
    else fputs("null", f);
    fputs(",\n  \"ids\": [", f);
    size_t w = 0;
    for (size_t k = 0; k < ids.n; k++) {
        if (k && !strcmp(ids.a[k], ids.a[k - 1])) continue;
        fputs(w++ ? ",\n    " : "\n    ", f);
        json_str(f, ids.a[k]);
    }
    fputs(w ? "\n  ]\n}\n" : "]\n}\n", f);
    fclose(f);
    fprintf(stderr, "backburn: wrote %s (%zu ids)\n", path, w);
    for (size_t k = 0; k < ids.n; k++) free(ids.a[k]);
    free(ids.a);
    return 0;
}
