/* The Prism visitor: turns one parsed file into definitions and named references. */
#include <ctype.h>

#include "index/indexer.h"
#include "core/intern.h"
#include "core/model.h"
#include "cli/options.h"
#include "index/erb.h"
#include "index/haml.h"
#include "index/walk.h"

typedef struct {
    pm_parser_t *p;
    const uint8_t *src;
    size_t srclen;
    uint32_t file;
    int is_test;
    sbuf ns;          /* current namespace, e.g. Admin::Reports */
    int singleton;    /* inside class << self */
    uint32_t cur;     /* def that owns references; 0 = load time */
    int64_t cur_name; /* enclosing method name, for super */
    int vis, next_def_vis;
    int in_eval;      /* depth of class_eval strings being read */
    uint32_t eval_line;
} ctx_t;

pm_options_t *OPTS;

static pm_visit_result_t visit(const pm_node_t *node, void *data);

static uint32_t line_at(ctx_t *c, uint32_t off) {
    if (c->in_eval) return c->eval_line;
    return (uint32_t) pm_line_offset_list_line_column(pm_parser_line_offsets(c->p), off,
                                                       pm_parser_start_line(c->p)).line;
}

static uint32_t end_off(const pm_node_t *n) {
    return n->location.length ? n->location.start + n->location.length - 1 : n->location.start;
}

/* Prism leaves id 0 on nodes it recovered from a syntax error. */
const pm_constant_t *safe_constant(pm_parser_t *p, pm_constant_id_t id) {
    return id && id <= pm_parser_constants_size(p) ? pm_parser_constant(p, id) : NULL;
}

static uint32_t cid(ctx_t *c, pm_constant_id_t id) {
    const pm_constant_t *k = safe_constant(c->p, id);
    if (!k) return intern("", 0);
    return intern((const char *) pm_constant_start(k), pm_constant_length(k));
}

const char *str_of(const pm_string_t *s, size_t *n) {
    *n = pm_string_length(s);
    return (const char *) pm_string_source(s);
}

static void slice(ctx_t *c, const pm_node_t *n, const char **s, size_t *len) {
    *s = (const char *) c->src + n->location.start;
    *len = n->location.length;
    if (*len >= 2 && (*s)[0] == ':' && (*s)[1] == ':') *s += 2, *len -= 2;
}

static void add_ref(ctx_t *c, uint32_t name, int is_const, uint32_t off) {
    if (c->is_test) {
        PUSH(TR, (tref_t){name, c->file, (uint8_t) is_const});
    } else {
        uint32_t line = line_at(c, off);
        PUSH(D.a[c->cur].refs, (ref_t){name, c->file, line, (uint8_t) is_const});
    }
}

static void add_ref_str(ctx_t *c, const char *s, size_t n, int is_const, uint32_t off) {
    if (n) add_ref(c, intern(s, n), is_const, off);
}

static int scan_nodoc(ctx_t *c, uint32_t off) {
    if (c->in_eval) return 0;
    const char *p = (const char *) c->src + off, *end = (const char *) c->src + c->srclen;
    if (end - p > 400) end = p + 400;
    for (const char *q = p; q + 7 <= end && *q != '\n'; q++)
        if (*q == ':' && !memcmp(q, ":nodoc:", 7)) return 1;
    return 0;
}

static uint32_t add_def(ctx_t *c, int kind, uint32_t name, const char *qual, size_t qn,
                        uint32_t off, uint32_t endoff, int vis) {
    def_t d;
    memset(&d, 0, sizeof d);
    d.kind = (uint8_t) kind;
    d.name = name;
    d.qual = intern(qual, qn);
    d.file = c->file;
    d.line = line_at(c, off);
    d.end_line = line_at(c, endoff);
    d.vis = (uint8_t) vis;
    d.super_ = d.outer = -1;
    d.parent = -1;
    d.root_rule = NOT_ROOT;
    d.batch = -1;
    d.nodoc = (uint8_t) scan_nodoc(c, off);
    PUSH(D, d);
    return (uint32_t) D.n - 1;
}

/* Constant-shaped: Foo or Foo::Bar, every segment starting A-Z. */
static int const_shaped(const char *s, size_t n) {
    if (!n) return 0;
    size_t i = 0;
    while (i < n) {
        if (!isupper((unsigned char) s[i])) return 0;
        while (i < n && is_ident_char((unsigned char) s[i])) i++;
        if (i == n) return 1;
        if (i + 2 < n + 1 && s[i] == ':' && i + 1 < n && s[i + 1] == ':') i += 2;
        else return 0;
    }
    return 0;
}

/* Method-shaped: 3-100 chars, [a-z_][a-z0-9_]* and an optional ? ! = */
static int meth_shaped(const char *s, size_t n) {
    if (n < 3 || n > 100) return 0;
    if (!(islower((unsigned char) s[0]) || s[0] == '_')) return 0;
    size_t end = n;
    if (s[n - 1] == '?' || s[n - 1] == '!' || s[n - 1] == '=') end--;
    for (size_t i = 1; i < end; i++)
        if (!(islower((unsigned char) s[i]) || isdigit((unsigned char) s[i]) || s[i] == '_')) return 0;
    return 1;
}

static void last_segment(const char *s, size_t n, const char **o, size_t *on) {
    size_t st = 0;
    for (size_t i = 0; i + 1 < n; i++)
        if (s[i] == ':' && s[i + 1] == ':') st = i + 2;
    *o = s + st;
    *on = n - st;
}

/* Interpolated strings and symbols become globs: literal parts as written
 * (? escaped), interpolations as *. Returns 1 with *is_const set when the
 * glob is a method pattern or a constant pattern (on its last segment). */
static int glob_of(const pm_node_list_t *parts, sbuf *out, int *is_const) {
    sb_trunc(out, 0);
    int lit = 0, meth_ok = 1, ident_ok = 1, upper = 0;
    for (uint32_t i = 0; i < parts->size; i++) {
        const pm_node_t *part = parts->nodes[i];
        if (PM_NODE_TYPE_P(part, PM_STRING_NODE)) {
            size_t n;
            const char *s = str_of(&((const pm_string_node_t *) part)->unescaped, &n);
            for (size_t k = 0; k < n; k++) {
                unsigned char ch = (unsigned char) s[k];
                lit++;
                if (!(islower(ch) || isdigit(ch) || ch == '_' || ch == '?' || ch == '!' || ch == '=')) meth_ok = 0;
                if (!(isalnum(ch) || ch == '_' || ch == ':')) ident_ok = 0;
                if (isupper(ch)) upper = 1;
                if (ch == '?') sb_puts(out, "\\?");
                else if (ch == '*' || ch == '[' || ch == '\\') sb_putc(out, '\\'), sb_putc(out, (char) ch);
                else sb_putc(out, (char) ch);
            }
        } else if (!out->n || out->s[out->n - 1] != '*') {
            sb_putc(out, '*');
        }
    }
    if (lit < 3) return 0;
    if (meth_ok) {
        *is_const = 0;
        return 1;
    }
    if (upper && ident_ok) {
        const char *seg;
        size_t sn;
        last_segment(out->s, out->n, &seg, &sn);
        int seglit = 0;
        for (size_t k = 0; k < sn; k++)
            if (seg[k] != '*') seglit++;
        if (seglit < 3) return 0;
        memmove(out->s, seg, sn);
        sb_trunc(out, sn);
        *is_const = 1;
        return 1;
    }
    return 0;
}

static const pm_node_list_t *interp_parts(const pm_node_t *n) {
    if (PM_NODE_TYPE_P(n, PM_INTERPOLATED_STRING_NODE)) return &((const pm_interpolated_string_node_t *) n)->parts;
    if (PM_NODE_TYPE_P(n, PM_INTERPOLATED_SYMBOL_NODE)) return &((const pm_interpolated_symbol_node_t *) n)->parts;
    return NULL;
}

static int yields_pattern(const pm_node_t *n, int want_const) {
    const pm_node_list_t *parts = interp_parts(n);
    if (!parts) return 0;
    sbuf b = {0};
    int k = 0, ok = glob_of(parts, &b, &k);
    free(b.s);
    return ok && k == want_const;
}

static void add_dyn(ctx_t *c, uint32_t name, uint32_t off, const pm_node_t *recv) {
    if (c->is_test || c->in_eval) return;
    dyn_t d = {c->file, line_at(c, off), S(name), -1, c->cur, 0, K_LOAD};
    if (name >= W_SEND && name <= W_PUBLIC_METHOD && c->ns.n && (!recv || PM_NODE_TYPE_P(recv, PM_SELF_NODE))) {
        d.self_ns = (int32_t) intern(c->ns.s, c->ns.n);
        d.public_only = name == W_PUBLIC_SEND || name == W_TRY || name == W_TRY_BANG || name == W_PUBLIC_METHOD;
        d.self_kind = c->cur ? D.a[c->cur].kind : K_LOAD;
    }
    PUSH(DYN, d);
}

/* class_eval "…" and friends: rebuild the source, parse it, visit it with
 * the same context. References count; definitions do not. */
static void eval_string(ctx_t *c, const pm_node_t *arg, uint32_t call_off) {
    sbuf b = {0};
    if (PM_NODE_TYPE_P(arg, PM_STRING_NODE)) {
        size_t n;
        const char *s = str_of(&((const pm_string_node_t *) arg)->unescaped, &n);
        sb_put(&b, s, n);
    } else {
        const pm_node_list_t *parts = interp_parts(arg);
        for (uint32_t i = 0; parts && i < parts->size; i++) {
            if (PM_NODE_TYPE_P(parts->nodes[i], PM_STRING_NODE)) {
                size_t n;
                const char *s = str_of(&((const pm_string_node_t *) parts->nodes[i])->unescaped, &n);
                sb_put(&b, s, n);
            } else {
                sb_puts(&b, "BackburnX"); /* valid as a constant, a method name, an ivar suffix */
            }
        }
    }
    if (!b.n) {
        free(b.s);
        return;
    }
    uint32_t line = line_at(c, call_off);
    pm_arena_t *arena = pm_arena_new();
    pm_parser_t *p = pm_parser_new(arena, (const uint8_t *) b.s, b.n, OPTS);
    pm_node_t *root = pm_parse(p);
    pm_parser_t *sp = c->p;
    const uint8_t *ssrc = c->src;
    size_t slen = c->srclen;
    uint32_t sline = c->eval_line;
    c->p = p;
    c->src = (const uint8_t *) b.s;
    c->srclen = b.n;
    c->eval_line = line;
    c->in_eval++;
    pm_visit_node(root, visit, c);
    c->in_eval--;
    c->p = sp;
    c->src = ssrc;
    c->srclen = slen;
    c->eval_line = sline;
    pm_parser_free(p);
    pm_arena_free(arena);
    free(b.s);
}

/* class / module: record the def, then visit the body inside its namespace. */
static void enter_ns(ctx_t *c, const pm_node_t *node, const pm_node_t *cpath, const pm_node_t *body,
                     int kind, const pm_node_t *super) {
    const char *t;
    size_t tn;
    slice(c, cpath, &t, &tn);
    size_t saved = c->ns.n;
    if (c->ns.n) sb_put(&c->ns, "::", 2);
    sb_put(&c->ns, t, tn);
    if (!c->is_test && !c->in_eval) {
        const char *seg;
        size_t sn;
        last_segment(t, tn, &seg, &sn);
        uint32_t i = add_def(c, kind, intern(seg, sn), c->ns.s, c->ns.n, cpath->location.start, end_off(node), V_PUBLIC);
        if (kind == K_CLASS) {
            D.a[i].outer = (int32_t) intern(c->ns.s, saved);
            const pm_node_t *s = super;
            if (s && PM_NODE_TYPE_P(s, PM_CALL_NODE)) s = ((const pm_call_node_t *) s)->receiver;
            if (s && (PM_NODE_TYPE_P(s, PM_CONSTANT_READ_NODE) || PM_NODE_TYPE_P(s, PM_CONSTANT_PATH_NODE))) {
                const char *st;
                size_t sl;
                slice(c, s, &st, &sl);
                D.a[i].super_ = (int32_t) intern(st, sl);
                D.a[i].super_top = (uint8_t) (st != (const char *) c->src + s->location.start);
            }
        }
    }
    int sing = c->singleton, vis = c->vis, ndv = c->next_def_vis;
    uint32_t cur = c->cur;
    int64_t cur_name = c->cur_name;
    c->singleton = 0;
    c->vis = V_PUBLIC;
    c->next_def_vis = -1;
    if (!c->in_eval) c->cur = 0;
    c->cur_name = -1;
    if (body) pm_visit_node(body, visit, c);
    c->singleton = sing;
    c->vis = vis;
    c->next_def_vis = ndv;
    c->cur = cur;
    c->cur_name = cur_name;
    sb_trunc(&c->ns, saved);
}

static void camel_refs(ctx_t *c, const char *s, size_t n, uint32_t off) {
    for (size_t i = 0; i < n; i++)
        if (!(islower((unsigned char) s[i]) || isdigit((unsigned char) s[i]) || s[i] == '_')) return;
    sbuf b = {0};
    int up = 1;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '_') {
            up = 1;
            continue;
        }
        sb_putc(&b, up ? (char) toupper((unsigned char) s[i]) : s[i]);
        up = 0;
    }
    if (b.n && isupper((unsigned char) b.s[0])) {
        add_ref_str(c, b.s, b.n, 1, off);
        if (b.n > 3 && has_suffix(b.s, "ies")) {
            sbuf y = {0};
            sb_put(&y, b.s, b.n - 3);
            sb_putc(&y, 'y');
            add_ref_str(c, y.s, y.n, 1, off);
            free(y.s);
        } else if (b.n > 1 && b.s[b.n - 1] == 's' && b.s[b.n - 2] != 's') {
            add_ref_str(c, b.s, b.n - 1, 1, off);
            if (b.n > 2 && b.s[b.n - 2] == 'e') add_ref_str(c, b.s, b.n - 2, 1, off);
        }
    }
    free(b.s);
}

/* RSpec predicate matchers: `be_active` calls active?, `have_key` calls has_key?. */
static void rspec_matcher_ref(ctx_t *c, uint32_t name, uint32_t off) {
    const char *s = S(name);
    size_t n = SLEN(name);
    sbuf b = {0};
    if (n > 3 && !strncmp(s, "be_", 3)) {
        sb_put(&b, s + 3, n - 3);
        sb_putc(&b, '?');
    } else if (n > 5 && !strncmp(s, "have_", 5)) {
        sb_puts(&b, "has_");
        sb_put(&b, s + 5, n - 5);
        sb_putc(&b, '?');
    }
    if (b.n) add_ref(c, intern(b.s, b.n), 0, off);
    free(b.s);
}

static void visit_call(ctx_t *c, const pm_node_t *node) {
    const pm_call_node_t *cn = (const pm_call_node_t *) node;
    uint32_t name = cid(c, cn->name);
    uint32_t off = cn->message_loc.length ? cn->message_loc.start : node->location.start;
    add_ref(c, name, 0, off);
    if (c->is_test) rspec_matcher_ref(c, name, off);
    if (name >= W_N) {
        /* def as argument: `register def self.handler(...)` */
        if (cn->arguments) {
            const pm_node_list_t *args = &cn->arguments->arguments;
            for (uint32_t i = 0; i < args->size; i++)
                if (PM_NODE_TYPE_P(args->nodes[i], PM_DEF_NODE))
                    add_ref(c, cid(c, ((const pm_def_node_t *) args->nodes[i])->name), 0, args->nodes[i]->location.start);
        }
        return;
    }
    const pm_node_list_t *args = cn->arguments ? &cn->arguments->arguments : NULL;
    const pm_node_t *a0 = args && args->size ? args->nodes[0] : NULL;
    int recvless = cn->receiver == NULL;

    if (name <= W_PUBLIC) {
        if (recvless && c->cur == 0) {
            int v = name == W_PRIVATE ? V_PRIVATE : name == W_PROTECTED ? V_PROTECTED : V_PUBLIC;
            if (!a0) c->vis = v;
            else if (PM_NODE_TYPE_P(a0, PM_DEF_NODE)) c->next_def_vis = v;
        }
    } else if (name > W_MODULE_FUNCTION && args) {
        for (uint32_t i = 0; i < args->size; i++)
            if (PM_NODE_TYPE_P(args->nodes[i], PM_DEF_NODE))
                add_ref(c, cid(c, ((const pm_def_node_t *) args->nodes[i])->name), 0, args->nodes[i]->location.start);
    }

    if ((name == W_INCLUDE || name == W_PREPEND || name == W_EXTEND) && recvless && c->ns.n && args &&
        !c->is_test && !c->in_eval) {
        uint32_t cls = intern(c->ns.s, c->ns.n);
        for (uint32_t i = 0; i < args->size; i++) {
            const pm_node_t *a = args->nodes[i];
            if (PM_NODE_TYPE_P(a, PM_CONSTANT_READ_NODE) || PM_NODE_TYPE_P(a, PM_CONSTANT_PATH_NODE)) {
                const char *t;
                size_t tn;
                slice(c, a, &t, &tn);
                PUSH(INC, (incl_t){cls, intern(t, tn), cls, -1});
            }
        }
    }

    if (name >= W_SEND && name <= W_DEFINE_METHOD && a0) {
        if (PM_NODE_TYPE_P(a0, PM_STRING_NODE)) {
            size_t n;
            const char *s = str_of(&((const pm_string_node_t *) a0)->unescaped, &n);
            add_ref_str(c, s, n, 0, a0->location.start);
        } else if (PM_NODE_TYPE_P(a0, PM_SYMBOL_NODE)) {
            /* the SymbolNode visit already references it */
        } else if (!yields_pattern(a0, 0)) {
            add_dyn(c, name, off, cn->receiver);
        }
    }

    if (name >= W_CONST_GET && name <= W_CONST_DEFINED) {
        const pm_node_t *arg = (name == W_CONSTANTIZE || name == W_SAFE_CONSTANTIZE) ? cn->receiver : a0;
        if (arg) {
            const char *s = NULL;
            size_t n = 0;
            if (PM_NODE_TYPE_P(arg, PM_STRING_NODE)) s = str_of(&((const pm_string_node_t *) arg)->unescaped, &n);
            else if (PM_NODE_TYPE_P(arg, PM_SYMBOL_NODE)) s = str_of(&((const pm_symbol_node_t *) arg)->unescaped, &n);
            if (s) {
                const char *seg;
                size_t sn;
                last_segment(s, n, &seg, &sn);
                add_ref_str(c, seg, sn, 1, arg->location.start);
            } else if (!yields_pattern(arg, 1)) {
                add_dyn(c, name, off, cn->receiver);
            }
        }
    }

    /* class_eval "…" and friends; whenever's `runner "Report.send_all"` runs its string too */
    if (((name >= W_CLASS_EVAL && name <= W_EVAL) || (name == W_RUNNER && recvless)) && a0 && c->in_eval < 3 &&
        (PM_NODE_TYPE_P(a0, PM_STRING_NODE) || PM_NODE_TYPE_P(a0, PM_INTERPOLATED_STRING_NODE)))
        eval_string(c, a0, node->location.start);

    if (name == W_REQUIRE && recvless && a0 && PM_NODE_TYPE_P(a0, PM_STRING_NODE) && !c->in_eval) {
        size_t n;
        const char *s = str_of(&((const pm_string_node_t *) a0)->unescaped, &n);
        PUSH(REQ, (req_t){c->file, xstrndup(s, n)});
    }
}

static pm_visit_result_t visit(const pm_node_t *node, void *data) {
    ctx_t *c = data;
    switch (PM_NODE_TYPE(node)) {
    case PM_CLASS_NODE: {
        const pm_class_node_t *n = (const pm_class_node_t *) node;
        if (n->superclass) pm_visit_node(n->superclass, visit, c);
        enter_ns(c, node, n->constant_path, n->body, K_CLASS, n->superclass);
        return PM_VISIT_SKIP;
    }
    case PM_MODULE_NODE: {
        const pm_module_node_t *n = (const pm_module_node_t *) node;
        enter_ns(c, node, n->constant_path, n->body, K_MODULE, NULL);
        return PM_VISIT_SKIP;
    }
    case PM_SINGLETON_CLASS_NODE: {
        const pm_singleton_class_node_t *n = (const pm_singleton_class_node_t *) node;
        if (n->expression && !PM_NODE_TYPE_P(n->expression, PM_SELF_NODE)) pm_visit_node(n->expression, visit, c);
        int sing = c->singleton, vis = c->vis, ndv = c->next_def_vis;
        c->singleton = 1;
        c->vis = V_PUBLIC;
        c->next_def_vis = -1;
        if (n->body) pm_visit_node(n->body, visit, c);
        c->singleton = sing;
        c->vis = vis;
        c->next_def_vis = ndv;
        return PM_VISIT_SKIP;
    }
    case PM_DEF_NODE: {
        const pm_def_node_t *n = (const pm_def_node_t *) node;
        uint32_t name = cid(c, n->name);
        int sing = c->singleton || n->receiver != NULL;
        /* a bare `private` doesn't reach `def self.x` */
        int vis = c->next_def_vis >= 0 ? c->next_def_vis : (n->receiver && !c->singleton) ? V_PUBLIC : c->vis;
        c->next_def_vis = -1;
        uint32_t cur = c->cur;
        int64_t cur_name = c->cur_name;
        if (!c->is_test && !c->in_eval) {
            sbuf q = {0};
            if (c->ns.n) sb_put(&q, c->ns.s, c->ns.n);
            else sb_puts(&q, "Object");
            sb_putc(&q, sing ? '.' : '#');
            sb_put(&q, S(name), SLEN(name));
            c->cur = add_def(c, sing ? K_SMETHOD : K_METHOD, name, q.s, q.n, n->name_loc.start, end_off(node), vis);
            free(q.s);
        }
        c->cur_name = name;
        pm_visit_child_nodes(node, visit, c);
        c->cur = cur;
        c->cur_name = cur_name;
        return PM_VISIT_SKIP;
    }
    case PM_CALL_NODE:
        visit_call(c, node);
        return PM_VISIT_DESCEND;
    case PM_SYMBOL_NODE: {
        size_t n;
        const char *s = str_of(&((const pm_symbol_node_t *) node)->unescaped, &n);
        if (n) {
            add_ref_str(c, s, n, 0, node->location.start);
            if (is_ident_char((unsigned char) s[n - 1])) {
                sbuf b = {0};
                sb_put(&b, s, n);
                sb_putc(&b, '=');
                add_ref_str(c, b.s, b.n, 0, node->location.start);
                free(b.s);
            }
            if (O.inflect) camel_refs(c, s, n, node->location.start);
        }
        return PM_VISIT_DESCEND;
    }
    case PM_STRING_NODE: {
        size_t n;
        const char *s = str_of(&((const pm_string_node_t *) node)->unescaped, &n);
        if (const_shaped(s, n)) {
            const char *seg;
            size_t sn;
            last_segment(s, n, &seg, &sn);
            add_ref_str(c, seg, sn, 1, node->location.start);
        } else if (meth_shaped(s, n)) {
            add_ref_str(c, s, n, 0, node->location.start);
        }
        return PM_VISIT_DESCEND;
    }
    case PM_CONSTANT_READ_NODE:
        add_ref(c, cid(c, ((const pm_constant_read_node_t *) node)->name), 1, node->location.start);
        return PM_VISIT_DESCEND;
    case PM_CONSTANT_PATH_NODE:
        add_ref(c, cid(c, ((const pm_constant_path_node_t *) node)->name), 1, node->location.start);
        return PM_VISIT_DESCEND;
    case PM_CONSTANT_WRITE_NODE: {
        const pm_constant_write_node_t *n = (const pm_constant_write_node_t *) node;
        if (c->cur == 0 && !c->is_test && !c->in_eval) {
            uint32_t name = cid(c, n->name);
            sbuf q = {0};
            if (c->ns.n) sb_put(&q, c->ns.s, c->ns.n), sb_put(&q, "::", 2);
            sb_put(&q, S(name), SLEN(name));
            add_def(c, K_CONST, name, q.s, q.n, n->name_loc.start, end_off(node), V_PUBLIC);
            free(q.s);
        }
        return PM_VISIT_DESCEND;
    }
    case PM_SUPER_NODE:
    case PM_FORWARDING_SUPER_NODE:
        if (c->cur_name >= 0) add_ref(c, (uint32_t) c->cur_name, 0, node->location.start);
        if (c->cur && !c->is_test && !c->in_eval) D.a[c->cur].overrides = 1;
        return PM_VISIT_DESCEND;
    case PM_INTERPOLATED_STRING_NODE:
    case PM_INTERPOLATED_SYMBOL_NODE: {
        sbuf g = {0};
        int is_const = 0;
        if (glob_of(interp_parts(node), &g, &is_const)) {
            PUSH(PREF, (pref_t){c->is_test ? -1 : (int32_t) c->cur, intern(g.s, g.n), c->file,
                                line_at(c, node->location.start), (uint8_t) is_const});
        }
        free(g.s);
        return PM_VISIT_DESCEND;
    }
    case PM_CALL_OPERATOR_WRITE_NODE: {
        const pm_call_operator_write_node_t *n = (const pm_call_operator_write_node_t *) node;
        add_ref(c, cid(c, n->read_name), 0, node->location.start);
        add_ref(c, cid(c, n->write_name), 0, node->location.start);
        return PM_VISIT_DESCEND;
    }
    case PM_CALL_OR_WRITE_NODE: {
        const pm_call_or_write_node_t *n = (const pm_call_or_write_node_t *) node;
        add_ref(c, cid(c, n->read_name), 0, node->location.start);
        add_ref(c, cid(c, n->write_name), 0, node->location.start);
        return PM_VISIT_DESCEND;
    }
    case PM_CALL_AND_WRITE_NODE: {
        const pm_call_and_write_node_t *n = (const pm_call_and_write_node_t *) node;
        add_ref(c, cid(c, n->read_name), 0, node->location.start);
        add_ref(c, cid(c, n->write_name), 0, node->location.start);
        return PM_VISIT_DESCEND;
    }
    default:
        return PM_VISIT_DESCEND;
    }
}

/* Sorbet's inline RBS signatures live in #: and #| comments. */
static void rbs_scan(ctx_t *c, const char *b, size_t n) {
    uint32_t cur = c->cur;
    c->cur = 0;
    for (size_t i = 0; i + 1 < n; i++) {
        if (b[i] != '#' || (b[i + 1] != ':' && b[i + 1] != '|')) continue;
        size_t j = i + 2;
        while (j < n && b[j] != '\n') {
            if (isupper((unsigned char) b[j]) && !is_ident_char((unsigned char) b[j - 1])) {
                size_t st = j;
                while (j < n && (is_ident_char((unsigned char) b[j]) ||
                                 (b[j] == ':' && j + 1 < n && b[j + 1] == ':')))
                    j += b[j] == ':' ? 2 : 1;
                size_t seg = st;
                for (size_t k = st; k <= j; k++) {
                    if (k == j || (b[k] == ':' && k + 1 < j && b[k + 1] == ':')) {
                        if (k > seg) add_ref_str(c, b + seg, k - seg, 1, (uint32_t) seg);
                        seg = k + 2;
                        k++;
                    }
                }
            } else {
                j++;
            }
        }
        i = j;
    }
    c->cur = cur;
}

void index_file(const char *abs, const char *rel, int kind) {
    size_t n;
    char *buf = read_file(abs, &n);
    if (!buf) return;
    uint32_t fi = (uint32_t) FILES.n;
    PUSH(FILES, xstrdup(rel));
    int is_test = any_glob(&O.tests, rel);
    PUSH(FTEST, (uint8_t) is_test);
    PUSH(FTEMPLATE, (uint8_t) (kind != FK_RUBY));
    if (kind == FK_TEMPLATE) erb_to_ruby(buf, n);
    if (kind == FK_HAML || kind == FK_SLIM) {
        size_t rn;
        char *ruby = haml_to_ruby(buf, n, kind == FK_SLIM, &rn);
        free(buf);
        buf = ruby;
        n = rn;
        kind = FK_TEMPLATE;
    }

    pm_arena_t *arena = pm_arena_new();
    pm_parser_t *p = pm_parser_new(arena, (const uint8_t *) buf, n, OPTS);
    pm_node_t *root = pm_parse(p);
    if (kind == FK_RUBY && pm_parser_errors_size(p) && memmem(buf, n, "<%", 2)) {
        /* old-style generator templates: ERB in a .rb file */
        pm_parser_free(p);
        pm_arena_free(arena);
        kind = FK_TEMPLATE;
        FTEMPLATE.a[fi] = 1;
        is_test = FTEST.a[fi] = 0; /* a generator template, even when named *_spec.rb */
        erb_to_ruby(buf, n);
        arena = pm_arena_new();
        p = pm_parser_new(arena, (const uint8_t *) buf, n, OPTS);
        root = pm_parse(p);
    }
    ctx_t c;
    memset(&c, 0, sizeof c);
    c.p = p;
    c.src = (const uint8_t *) buf;
    c.srclen = n;
    c.file = fi;
    c.is_test = is_test;
    c.cur_name = -1;
    c.next_def_vis = -1;
    pm_visit_node(root, visit, &c);
    rbs_scan(&c, buf, n);
    if (pm_parser_errors_size(p)) {
        if (kind == FK_TEMPLATE) {
            TEMPLATE_ERRORS++;
        } else {
            const pm_diagnostic_t *e = pm_parser_errors_first(p);
            PUSH(PERR, (perr_t){fi, line_at(&c, pm_diagnostic_location(e).start), xstrdup(pm_diagnostic_message(e))});
        }
    }
    free(c.ns.s);
    pm_parser_free(p);
    pm_arena_free(arena);
    free(buf);
}
