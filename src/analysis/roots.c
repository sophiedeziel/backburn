/* Root rules: the entrances to the maze (presets, roots files, hooks, --public-api). */
#include <ctype.h>
#include <errno.h>
#include <fnmatch.h>

#include "analysis/roots.h"
#include "core/intern.h"
#include "analysis/graph.h"
#include "cli/options.h"
#include "presets.h"

rule_vec RULES;

strvec ROOT_SOURCES; /* for the header: "rails", "rails.roots" … */

static const char *const HOOKS[] = {
    "initialize", "initialize_copy", "initialize_dup", "initialize_clone", "method_missing",
    "respond_to_missing?", "respond_to?", "to_s", "to_str", "inspect", "to_proc", "to_a", "to_ary",
    "to_h", "to_hash", "to_i", "to_int", "to_f", "to_r", "to_c", "to_sym", "to_json", "as_json",
    "to_param", "call", "each", "hash", "eql?", "coerce", "inherited", "included", "extended",
    "prepended", "method_added", "singleton_method_added", "const_missing", "marshal_dump",
    "marshal_load", "_dump", "_load", "encode_with", "init_with", "pretty_print", "freeze", "dup",
    "clone", "===", "==", "<=>", NULL};

uint8_t *IS_HOOK;

int32_t *FILE_PATH_RULE;

static void load_rules(const char *text, const char *label) {
    int lineno = 0;
    const char *p = text;
    PUSH(ROOT_SOURCES, xstrdup(label));
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t) (e - p) : strlen(p);
        lineno++;
        const char *s = p;
        while (n && isspace((unsigned char) *s)) s++, n--;
        while (n && isspace((unsigned char) s[n - 1])) n--;
        p = e ? e + 1 : p + strlen(p);
        if (!n || s[0] == '#') continue;
        rule_t r;
        memset(&r, 0, sizeof r);
        r.text = xstrndup(s, n);
        sbuf src = {0};
        sb_printf(&src, "%s:%d", label, lineno);
        r.source = src.s;
        const char *q = r.text;
        if (!strncmp(q, "public ", 7)) {
            r.pub = 1;
            q += 7;
            while (*q == ' ') q++;
        }
        if (q[0] == '<' && (q[1] == ' ' || q[1] == '\t')) {
            r.type = RULE_KIND;
            q += 1;
            while (*q == ' ' || *q == '\t') q++;
            if (!strncmp(q, "::", 2)) q += 2;
        } else if (!strncmp(q, "path:", 5)) {
            r.type = RULE_PATH;
            q += 5;
        } else {
            r.type = RULE_PAT;
        }
        r.pat = xstrdup(q);
        PUSH(RULES, r);
    }
}

static int kind_direct(const char *pat, const char *owner, int64_t oid) {
    if (!fnmatch(pat, owner, 0)) return 1;
    if (oid < 0 || oid >= NN) return 0;
    int32_t cd = CLS_BY_QUAL[oid];
    for (int step = 0; cd >= 0 && step < 32; step++) {
        const def_t *d = &D.a[cd];
        if (d->kind != K_CLASS || d->super_ < 0) break;
        if (!fnmatch(pat, S(d->super_), 0)) return 1;
        int32_t r = SUPER_DEF[cd];
        if (r < 0) break;
        if (!fnmatch(pat, S(D.a[r].qual), 0)) return 1;
        cd = CLS_BY_QUAL[D.a[r].qual];
    }
    CSR_FOR(INC_BY_CLS, (uint32_t) oid, k) {
        const incl_t *in = &INC.a[k];
        if (!fnmatch(pat, S(in->mod), 0)) return 1;
        if (in->res >= 0 && !fnmatch(pat, S(D.a[in->res].qual), 0)) return 1;
    }
    return 0;
}

static int kind_of(rule_t *r, const char *owner, int64_t oid) {
    if (oid >= 0 && oid < NN) {
        if (!r->memo) r->memo = xcalloc(NN, 1);
        if (r->memo[oid]) return r->memo[oid] == 1;
    }
    int res = kind_direct(r->pat, owner, oid);
    if (!res && oid >= 0 && oid < NN) {
        CSR_FOR(INC_BY_RES, (uint32_t) oid, k) {
            uint32_t cls = INC.a[k].cls;
            if (kind_direct(r->pat, S(cls), cls)) {
                res = 1;
                break;
            }
        }
    }
    if (oid >= 0 && oid < NN) r->memo[oid] = res ? 1 : 2;
    return res;
}

/* Does a non-path rule match definition i? (public only restricts methods) */
int rule_matches(rule_t *r, uint32_t i) {
    const def_t *d = &D.a[i];
    int meth = is_meth(d->kind);
    if (r->pub && meth && d->vis != V_PUBLIC) return 0;
    const char *q = S(d->qual);
    if (r->type == RULE_PAT) return !fnmatch(r->pat, q, 0);
    if (r->type != RULE_KIND) return 0;
    size_t on = SLEN(d->qual);
    if (meth) {
        const char *cut = strrchr(q, '#');
        if (!cut) cut = strrchr(q, '.');
        if (cut) on = (size_t) (cut - q);
    } else if (d->kind == K_CONST) {
        /* a constant belongs to its namespace: cut at the last `::` */
        for (size_t j = on; j >= 2; j--) {
            if (q[j - 1] == ':' && q[j - 2] == ':') {
                on = j - 2;
                break;
            }
        }
    }
    char stackbuf[512];
    char *owner = on < sizeof stackbuf ? stackbuf : xrealloc(NULL, on + 1);
    memcpy(owner, q, on);
    owner[on] = 0;
    int res = kind_of(r, owner, find_name(owner, on));
    if (owner != stackbuf) free(owner);
    return res;
}

int is_operator(uint32_t name) {
    unsigned char c0 = (unsigned char) S(name)[0];
    return !(isalpha(c0) || c0 == '_' || c0 >= 0x80);
}

/* Built-in root checks, in the spec's order. Returns a rule code. */
int32_t root_for(uint32_t i) {
    const def_t *d = &D.a[i];
    if (d->kind == K_LOAD) return RR_LOAD;
    if (FILE_PATH_RULE[d->file] >= 0) return FILE_PATH_RULE[d->file];
    if (is_meth(d->kind) && d->overrides) return RR_SUPER;
    if (is_constk(d->kind)) {
        if (O.public_api && !d->nodoc) return RR_PUBLIC_API;
        for (size_t r = 0; r < RULES.n; r++)
            if (RULES.a[r].type != RULE_PATH && rule_matches(&RULES.a[r], i)) return (int32_t) r;
        return NOT_ROOT;
    }
    if (is_operator(d->name)) return RR_OPERATOR;
    if (d->name < NN && IS_HOOK[d->name]) return RR_HOOK;
    if (O.public_api && d->vis == V_PUBLIC && !d->nodoc) return RR_PUBLIC_API;
    for (size_t r = 0; r < RULES.n; r++)
        if (RULES.a[r].type != RULE_PATH && rule_matches(&RULES.a[r], i)) return (int32_t) r;
    return NOT_ROOT;
}

void prepare_roots(void) {
    IS_HOOK = xcalloc(NN + 1, 1);
    for (int k = 0; HOOKS[k]; k++) {
        int64_t id = find_name(HOOKS[k], strlen(HOOKS[k]));
        if (id >= 0 && id < NN) IS_HOOK[id] = 1;
    }
    FILE_PATH_RULE = xcalloc(FILES.n + 1, sizeof *FILE_PATH_RULE);
    for (size_t f = 0; f < FILES.n; f++) {
        FILE_PATH_RULE[f] = -1;
        for (size_t r = 0; r < RULES.n; r++)
            if (RULES.a[r].type == RULE_PATH && !fnmatch(RULES.a[r].pat, FILES.a[f], 0)) {
                FILE_PATH_RULE[f] = (int32_t) r;
                break;
            }
    }
}

void rule_desc(int32_t rr, sbuf *b) {
    switch (rr) {
    case RR_LOAD: sb_puts(b, "code outside any method runs when its file loads"); break;
    case RR_SUPER: sb_puts(b, "calls super, so whoever calls the parent method reaches it"); break;
    case RR_OPERATOR: sb_puts(b, "operator method"); break;
    case RR_HOOK: sb_puts(b, "Ruby calls this hook by convention"); break;
    case RR_PUBLIC_API: sb_puts(b, "public API (--public-api)"); break;
    default:
        if (rr >= 0 && (size_t) rr < RULES.n) sb_printf(b, "%s  (%s)", RULES.a[rr].text, RULES.a[rr].source);
    }
}

const char *preset_text(const char *name) {
    for (int k = 0; PRESETS[k].name; k++)
        if (!strcmp(PRESETS[k].name, name)) return PRESETS[k].text;
    return NULL;
}

void load_all_rules(void) {
    for (size_t k = 0; k < O.presets.n; k++) {
        const char *t = preset_text(O.presets.a[k]);
        if (!t) {
            fprintf(stderr, "backburn: unknown preset %s (available:", O.presets.a[k]);
            for (int p = 0; PRESETS[p].name; p++) fprintf(stderr, " %s", PRESETS[p].name);
            fputs(")\n", stderr);
            exit(2);
        }
        load_rules(t, O.presets.a[k]);
    }
    for (size_t k = 0; k < O.roots.n; k++) {
        size_t n;
        char *t = read_file(O.roots.a[k], &n);
        if (!t) {
            fprintf(stderr, "backburn: cannot read roots file %s: %s\n", O.roots.a[k], strerror(errno));
            exit(2);
        }
        load_rules(t, O.roots.a[k]);
        free(t);
    }
}
