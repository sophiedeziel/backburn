/* Reading Gemfile and Gemfile.lock. */

#include "commands/gemfile.h"
#include "index/indexer.h"

gem_vec GEMS;

typedef struct { pm_parser_t *p; strvec groups; } gemfile_ctx;

static char *node_text(pm_parser_t *p, const pm_node_t *n) {
    size_t len;
    if (PM_NODE_TYPE_P(n, PM_STRING_NODE)) {
        const char *s = str_of(&((const pm_string_node_t *) n)->unescaped, &len);
        return xstrndup(s, len);
    }
    if (PM_NODE_TYPE_P(n, PM_SYMBOL_NODE)) {
        const char *s = str_of(&((const pm_symbol_node_t *) n)->unescaped, &len);
        return xstrndup(s, len);
    }
    return xstrndup((const char *) pm_parser_start(p) + n->location.start, n->location.length);
}

static void kw_groups(pm_parser_t *p, const pm_node_t *v, sbuf *out) {
    if (PM_NODE_TYPE_P(v, PM_ARRAY_NODE)) {
        const pm_node_list_t *el = &((const pm_array_node_t *) v)->elements;
        for (uint32_t i = 0; i < el->size; i++) kw_groups(p, el->nodes[i], out);
        return;
    }
    char *t = node_text(p, v);
    if (out->n) sb_putc(out, ',');
    sb_puts(out, t);
    free(t);
}

static pm_visit_result_t gemfile_visit(const pm_node_t *node, void *data) {
    gemfile_ctx *g = data;
    if (!PM_NODE_TYPE_P(node, PM_CALL_NODE)) return PM_VISIT_DESCEND;
    const pm_call_node_t *cn = (const pm_call_node_t *) node;
    if (cn->receiver) return PM_VISIT_DESCEND;
    const pm_constant_t *k = safe_constant(g->p, cn->name);
    if (!k) return PM_VISIT_DESCEND;
    size_t kn = pm_constant_length(k);
    const char *ks = (const char *) pm_constant_start(k);
    const pm_node_list_t *args = cn->arguments ? &cn->arguments->arguments : NULL;
    if (kn == 5 && !memcmp(ks, "group", 5) && cn->block) {
        size_t saved = g->groups.n;
        for (uint32_t i = 0; args && i < args->size; i++)
            if (PM_NODE_TYPE_P(args->nodes[i], PM_SYMBOL_NODE) || PM_NODE_TYPE_P(args->nodes[i], PM_STRING_NODE))
                PUSH(g->groups, node_text(g->p, args->nodes[i]));
        pm_visit_node(cn->block, gemfile_visit, g);
        while (g->groups.n > saved) free(g->groups.a[--g->groups.n]);
        return PM_VISIT_SKIP;
    }
    if (kn == 3 && !memcmp(ks, "gem", 3) && args && args->size && PM_NODE_TYPE_P(args->nodes[0], PM_STRING_NODE)) {
        gem_t gm;
        memset(&gm, 0, sizeof gm);
        gm.name = node_text(g->p, args->nodes[0]);
        sbuf groups = {0};
        for (size_t i = 0; i < g->groups.n; i++) {
            if (groups.n) sb_putc(&groups, ',');
            sb_puts(&groups, g->groups.a[i]);
        }
        for (uint32_t i = 1; i < args->size; i++) {
            if (!PM_NODE_TYPE_P(args->nodes[i], PM_KEYWORD_HASH_NODE)) continue;
            const pm_node_list_t *el = &((const pm_keyword_hash_node_t *) args->nodes[i])->elements;
            for (uint32_t e = 0; e < el->size; e++) {
                if (!PM_NODE_TYPE_P(el->nodes[e], PM_ASSOC_NODE)) continue;
                const pm_assoc_node_t *as = (const pm_assoc_node_t *) el->nodes[e];
                char *key = node_text(g->p, as->key);
                if (!strcmp(key, "group") || !strcmp(key, "groups")) kw_groups(g->p, as->value, &groups);
                if (!strcmp(key, "require") && PM_NODE_TYPE_P(as->value, PM_FALSE_NODE)) gm.require_false = 1;
                free(key);
            }
        }
        gm.groups = groups.s ? groups.s : xstrdup("default");
        PUSH(GEMS, gm);
        return PM_VISIT_SKIP;
    }
    return PM_VISIT_DESCEND;
}

int parse_gemfile(const char *path) {
    size_t n;
    char *buf = read_file(path, &n);
    if (!buf) return 0;
    pm_arena_t *arena = pm_arena_new();
    pm_parser_t *p = pm_parser_new(arena, (const uint8_t *) buf, n, OPTS);
    pm_node_t *root = pm_parse(p);
    gemfile_ctx g = {p, {0}};
    pm_visit_node(root, gemfile_visit, &g);
    free(g.groups.a);
    pm_parser_free(p);
    pm_arena_free(arena);
    free(buf);
    return 1;
}

lockspec_vec LOCK;

int parse_lock(const char *path) {
    size_t n;
    char *t = read_file(path, &n);
    if (!t) return 0;
    lockspec_t *cur = NULL;
    for (char *p = t; *p;) {
        char *e = strchr(p, '\n');
        size_t len = e ? (size_t) (e - p) : strlen(p);
        char *line = p;
        p = e ? e + 1 : p + len;
        size_t ind = 0;
        while (ind < len && line[ind] == ' ') ind++;
        if (ind == len) continue;
        if (ind == 0) cur = NULL;
        size_t nl = strcspn(line + ind, " (!\r\n");
        if (nl > len - ind) nl = len - ind;
        if (ind == 4) {
            lockspec_t s = {xstrndup(line + ind, nl), {0}};
            PUSH(LOCK, s);
            cur = &LOCK.a[LOCK.n - 1];
        } else if (ind == 6 && cur) {
            PUSH(cur->deps, xstrndup(line + ind, nl));
        }
    }
    free(t);
    return 1;
}
