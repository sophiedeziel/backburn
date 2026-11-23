/* gems: which Gemfile gems does the app still need? */
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "commands/commands.h"
#include "commands/gemfile.h"
#include "core/intern.h"
#include "core/model.h"
#include "core/json.h"
#include "cli/options.h"
#include "index/indexer.h"

/* Ruby's own constants, and framework namespaces plugins reopen: a gem that
 * reopens String or Rails says nothing about whether the app uses it. */
static const char *const CORE_CONSTS[] = {
    "Object", "BasicObject", "Kernel", "Module", "Class", "String", "Symbol", "Array", "Hash", "Integer",
    "Float", "Numeric", "Rational", "Complex", "Range", "Regexp", "NilClass", "TrueClass", "FalseClass",
    "Proc", "Method", "Comparable", "Enumerable", "Enumerator", "Exception", "StandardError", "RuntimeError",
    "ArgumentError", "Struct", "Data", "Time", "Date", "DateTime", "File", "Dir", "IO", "Process", "Thread",
    "Math", "Marshal", "ObjectSpace", "Signal", "Warning", "Encoding", "Random", "URI", "Net", "JSON", "YAML",
    "Psych", "Set", "Pathname", "Logger", "OpenSSL", "Digest", "Base64", "ERB", "CGI", "Socket", "Gem", "Bundler",
    "Rails", "ActiveSupport", "ActiveRecord", "ActiveModel", "ActionController", "ActionView", "ActionDispatch",
    "ActionMailer", "ActiveJob", "ActionCable", "ActiveStorage", "ActionText", "ActionMailbox", "Rack", NULL};

static int is_core_const(const char *s) {
    for (int k = 0; CORE_CONSTS[k]; k++)
        if (!strcmp(CORE_CONSTS[k], s)) return 1;
    return 0;
}

static void norm_name(const char *s, char *out, size_t cap) {
    size_t n = 0;
    for (; *s && n + 1 < cap; s++)
        if (isalnum((unsigned char) *s)) out[n++] = (char) tolower((unsigned char) *s);
    out[n] = 0;
}

/* Does gem `name` own constant `c`? (graphql owns GraphQL, activesupport owns ActiveSupport) */
static int gem_owns(const char *name, const char *c) {
    char a[256], b[256];
    norm_name(name, a, sizeof a);
    norm_name(c, b, sizeof b);
    return *a && *b && (!strncmp(a, b, strlen(b)) || !strncmp(b, a, strlen(a)));
}

static void add_unique(u32vec *v, uint32_t id) {
    for (size_t k = 0; k < v->n; k++)
        if (v->a[k] == id) return;
    PUSH(*v, id);
}

static void collect_tops(const char *dir, gem_t *g, int depth) {
    if (depth > 12) return;
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        sbuf p = {0};
        sb_puts(&p, dir);
        sb_putc(&p, '/');
        sb_puts(&p, e->d_name);
        struct stat st;
        if (!stat(p.s, &st)) {
            if (S_ISDIR(st.st_mode)) collect_tops(p.s, g, depth + 1);
            else if (has_suffix(e->d_name, ".rb")) {
                size_t n;
                char *buf = read_file(p.s, &n);
                if (buf) {
                    if (strstr(buf, "Rails::Railtie") || strstr(buf, "Rails::Engine")) g->railtie = 1;
                    pm_arena_t *arena = pm_arena_new();
                    pm_parser_t *pp = pm_parser_new(arena, (const uint8_t *) buf, n, OPTS);
                    pm_node_t *root = pm_parse(pp);
                    const pm_statements_node_t *sts = ((const pm_program_node_t *) root)->statements;
                    for (uint32_t i = 0; sts && i < sts->body.size; i++) {
                        const pm_node_t *s = sts->body.nodes[i];
                        const pm_node_t *cp = PM_NODE_TYPE_P(s, PM_CLASS_NODE) ? ((const pm_class_node_t *) s)->constant_path
                                            : PM_NODE_TYPE_P(s, PM_MODULE_NODE) ? ((const pm_module_node_t *) s)->constant_path
                                                                                 : NULL;
                        if (!cp) continue;
                        const char *t = (const char *) buf + cp->location.start;
                        size_t tn = cp->location.length;
                        if (tn >= 2 && t[0] == ':') t += 2, tn -= 2;
                        size_t first = 0;
                        while (first < tn && t[first] != ':') first++;
                        uint32_t id = intern(t, first);
                        add_unique(&g->tops, id);
                        const pm_node_t *body = PM_NODE_TYPE_P(s, PM_CLASS_NODE) ? ((const pm_class_node_t *) s)->body
                                                                                  : ((const pm_module_node_t *) s)->body;
                        if (first == tn && is_core_const(S(id)) && body && PM_NODE_TYPE_P(body, PM_STATEMENTS_NODE)) {
                            const pm_node_list_t *bl = &((const pm_statements_node_t *) body)->body;
                            for (uint32_t b = 0; b < bl->size; b++)
                                if (PM_NODE_TYPE_P(bl->nodes[b], PM_DEF_NODE)) {
                                    const pm_constant_t *k = safe_constant(pp, ((const pm_def_node_t *) bl->nodes[b])->name);
                                    if (k) add_unique(&g->core_meths, intern((const char *) pm_constant_start(k), pm_constant_length(k)));
                                }
                        }
                    }
                    pm_parser_free(pp);
                    pm_arena_free(arena);
                    free(buf);
                }
            }
        }
        free(p.s);
    }
    closedir(d);
}

static char *camelize_gem(const char *name, int first_only) {
    sbuf b = {0};
    int up = 1;
    for (const char *s = name; *s; s++) {
        if (*s == '-') {
            if (first_only) break;
            sb_puts(&b, "::");
            up = 1;
        } else if (*s == '_') {
            up = 1;
        } else {
            sb_putc(&b, up ? (char) toupper((unsigned char) *s) : *s);
            up = 0;
        }
    }
    return b.s ? b.s : xstrdup("");
}

int cmd_gems(void) {
    char *gemfile = root_path("Gemfile"), *lock = root_path("Gemfile.lock");
    if (!parse_gemfile(gemfile)) {
        fprintf(stderr, "backburn: no Gemfile in %s\n", O.root);
        return 2;
    }
    parse_lock(lock);
    /* where each gem's source lives */
    strvec all_names = {0}, all_paths = {0};
    int bundle = 0;
    size_t nread = 0;
    if (O.gem_src) {
        DIR *d = opendir(O.gem_src);
        if (!d) {
            fprintf(stderr, "backburn: cannot read --gem-src %s\n", O.gem_src);
            return 2;
        }
        struct dirent *e;
        while ((e = readdir(d))) {
            /* name-version, or name-version-platform (nokogiri-1.16.0-arm64-darwin) */
            for (size_t g = 0; g < GEMS.n; g++) {
                size_t ln = strlen(GEMS.a[g].name);
                if (!strncmp(GEMS.a[g].name, e->d_name, ln) && e->d_name[ln] == '-' && isdigit((unsigned char) e->d_name[ln + 1])) {
                    sbuf p = {0};
                    sb_printf(&p, "%s/%s", O.gem_src, e->d_name);
                    free(GEMS.a[g].path);
                    GEMS.a[g].path = p.s;
                }
            }
        }
        closedir(d);
        bundle = 1;
    } else if (access(lock, R_OK) == 0) {
        sbuf cmd = {0};
        sb_puts(&cmd, "cd -- ");
        sh_quote(&cmd, O.root);
        sb_puts(&cmd, " && bundle exec ruby -e 'Bundler.load.specs.each { |s| puts \"#{s.name}\\t#{s.full_gem_path}\" }' 2>/dev/null");
        FILE *p = popen(cmd.s, "r");
        free(cmd.s);
        if (p) {
            char line[4096];
            while (fgets(line, sizeof line, p)) {
                line[strcspn(line, "\r\n")] = 0;
                char *tab = strchr(line, '\t');
                if (!tab) continue;
                *tab = 0;
                bundle = 1;
                PUSH(all_names, xstrdup(line));
                PUSH(all_paths, xstrdup(tab + 1));
                for (size_t g = 0; g < GEMS.n; g++)
                    if (!strcmp(GEMS.a[g].name, line)) {
                        free(GEMS.a[g].path);
                        GEMS.a[g].path = xstrdup(tab + 1);
                    }
            }
            if (pclose(p) != 0) bundle = 0;
        }
    }
    /* what the app uses: constants and method names it references (tests included) */
    uint8_t *app_const = xcalloc(I.n + 1, 1), *app_meth = xcalloc(I.n + 1, 1);
    size_t napp = I.n;
    for (size_t d = 0; d < D.n; d++)
        for (size_t k = 0; k < D.a[d].refs.n; k++)
            (D.a[d].refs.a[k].is_const ? app_const : app_meth)[D.a[d].refs.a[k].name] = 1;
    for (size_t k = 0; k < TR.n; k++) (TR.a[k].is_const ? app_const : app_meth)[TR.a[k].name] = 1;
    for (size_t d = 1; d < D.n; d++)
        if (is_meth(D.a[d].kind)) app_meth[D.a[d].name] = 0; /* the app defines it itself */
    /* config files that name gems without requiring them */
    static const char *CONFIG_FILES[] = {".rubocop.yml", "config/database.yml", "config/cable.yml", "Procfile",
                                         "Procfile.dev", ".overcommit.yml", "Guardfile", "config.ru", NULL};
    char *config_text[16] = {0};
    for (int k = 0; CONFIG_FILES[k]; k++) {
        char *p = root_path(CONFIG_FILES[k]);
        size_t n;
        config_text[k] = read_file(p, &n);
        free(p);
    }
    for (size_t g = 0; g < GEMS.n; g++) {
        gem_t *gm = &GEMS.a[g];
        if (gm->path) {
            nread++;
            sbuf lib = {0};
            sb_printf(&lib, "%s/lib", gm->path);
            collect_tops(lib.s, gm, 0);
            free(lib.s);
            if (!gm->tops.n) /* a meta-gem (sorbet-static-and-runtime): judge it by what its deps define */
                for (size_t l = 0; l < LOCK.n; l++) {
                    if (strcmp(LOCK.a[l].name, gm->name)) continue;
                    for (size_t k = 0; k < LOCK.a[l].deps.n; k++)
                        for (size_t a = 0; a < all_names.n; a++)
                            if (!strcmp(all_names.a[a], LOCK.a[l].deps.a[k])) {
                                sbuf dl = {0};
                                sb_printf(&dl, "%s/lib", all_paths.a[a]);
                                collect_tops(dl.s, gm, 0);
                                free(dl.s);
                            }
                }
            struct stat st;
            sbuf ex = {0};
            sb_printf(&ex, "%s/exe", gm->path);
            if (!stat(ex.s, &st) && S_ISDIR(st.st_mode)) gm->executable = 1;
            sb_trunc(&ex, 0);
            sb_printf(&ex, "%s/bin", gm->path);
            if (!gm->executable && !stat(ex.s, &st) && S_ISDIR(st.st_mode)) gm->executable = 1;
            free(ex.s);
        }
    }
    /* how many declared gems define each top-level constant */
    for (size_t g = 0; g < GEMS.n; g++) {
        gem_t *gm = &GEMS.a[g];
        sbuf slash = {0};
        sb_puts(&slash, gm->name);
        for (size_t k = 0; k < slash.n; k++)
            if (slash.s[k] == '-') slash.s[k] = '/';
        for (size_t r = 0; r < REQ.n; r++) {
            const char *q = REQ.a[r].req;
            size_t ln = strlen(gm->name);
            if (!strcmp(q, gm->name) || !strcmp(q, slash.s) || (!strncmp(q, gm->name, ln) && q[ln] == '/') ||
                (!strncmp(q, slash.s, slash.n) && q[slash.n] == '/')) {
                sbuf ev = {0};
                sb_printf(&ev, "require %s (%s)", q, FILES.a[REQ.a[r].file]);
                size_t e;
                for (e = 0; e < gm->evidence.n && strncmp(gm->evidence.a[e], "require ", 8); e++) {}
                if (e == gm->evidence.n) PUSH(gm->evidence, ev.s);
                else free(ev.s);
            }
        }
        free(slash.s);
        u32vec guess = {0};
        if (!gm->path) {
            char *full = camelize_gem(gm->name, 0), *top = camelize_gem(gm->name, 1);
            const char *last = strrchr(full, ':');
            last = last ? last + 1 : full;
            PUSH(guess, intern_cstr(top));
            if (strcmp(last, top)) PUSH(guess, intern_cstr(last));
            free(full);
            free(top);
        }
        const u32vec *tops = gm->path ? &gm->tops : &guess;
        size_t shown = 0;
        for (size_t t = 0; t < tops->n; t++) {
            uint32_t id = tops->a[t];
            if (id >= napp || !app_const[id]) continue;
            size_t owners = 0;
            for (size_t h = 0; h < GEMS.n; h++)
                for (size_t q = 0; q < GEMS.a[h].tops.n; q++) owners += GEMS.a[h].tops.a[q] == id;
            if (gm->path && owners > 2) continue; /* reopened by many gems */
            if (is_core_const(S(id)) && !gem_owns(gm->name, S(id))) continue;
            if (shown++ < 3) {
                sbuf ev = {0};
                sb_printf(&ev, "constant %s", S(id));
                PUSH(gm->evidence, ev.s);
            }
        }
        free(guess.a);
        size_t mshown = 0;
        for (size_t m = 0; m < gm->core_meths.n && !shown; m++) {
            uint32_t id = gm->core_meths.a[m];
            if (id >= napp || !app_meth[id] || mshown++ >= 3) continue;
            sbuf ev = {0};
            sb_printf(&ev, "method %s", S(id));
            PUSH(gm->evidence, ev.s);
        }
        for (int k = 0; CONFIG_FILES[k]; k++) {
            const char *t = config_text[k];
            const char *want = !strcmp(gm->name, "pg") ? "postgresql" : gm->name;
            size_t wl = strlen(want);
            for (const char *q = t ? strstr(t, want) : NULL; q; q = strstr(q + 1, want)) {
                int lb = q == t || !(isalnum((unsigned char) q[-1]) || q[-1] == '_' || q[-1] == '-');
                int rb = !(isalnum((unsigned char) q[wl]) || q[wl] == '_' || q[wl] == '-');
                const char *ls = q;
                while (ls > t && ls[-1] != '\n') ls--;
                const char *hash = memchr(ls, '#', (size_t) (q - ls));
                if (lb && rb && !hash) {
                    sbuf ev = {0};
                    sb_printf(&ev, "named in %s", CONFIG_FILES[k]);
                    PUSH(gm->evidence, ev.s);
                    break;
                }
            }
        }
        {
            sbuf cf = {0};
            sb_printf(&cf, "config/%s.rb", gm->name);
            char *p = root_path(cf.s);
            if (access(p, F_OK) == 0) {
                sbuf ev = {0};
                sb_printf(&ev, "configured in %s", cf.s);
                PUSH(gm->evidence, ev.s);
            }
            free(p);
            free(cf.s);
        }
        for (size_t l = 0; l < LOCK.n; l++)
            for (size_t k = 0; k < LOCK.a[l].deps.n; k++)
                if (!strcmp(LOCK.a[l].deps.a[k], gm->name)) {
                    size_t e; /* a gem listed once per platform appears once */
                    for (e = 0; e < gm->needed_by.n && strcmp(gm->needed_by.a[e], LOCK.a[l].name); e++) {}
                    if (e == gm->needed_by.n) PUSH(gm->needed_by, xstrdup(LOCK.a[l].name));
                }
        if (gm->evidence.n) gm->verdict = "used";
        else if (gm->railtie) {
            gm->verdict = "auto";
            PUSH(gm->evidence, xstrdup("railtie: loads itself into Rails"));
        } else if (gm->executable) {
            gm->verdict = "tool";
            PUSH(gm->evidence, xstrdup("ships executables"));
        } else {
            gm->verdict = "unused";
        }
    }
    free(app_const);
    free(app_meth);
    for (int k = 0; CONFIG_FILES[k]; k++) free(config_text[k]);

    if (O.format == F_TSV) {
        fputs("gem\tgroups\tverdict\tevidence\tneeded_by\n", OUT);
        for (size_t g = 0; g < GEMS.n; g++) {
            gem_t *gm = &GEMS.a[g];
            fprintf(OUT, "%s\t%s\t%s\t", gm->name, gm->groups, gm->verdict);
            for (size_t e = 0; e < gm->evidence.n; e++) fprintf(OUT, "%s%s", e ? "; " : "", gm->evidence.a[e]);
            fputc('\t', OUT);
            for (size_t e = 0; e < gm->needed_by.n; e++) fprintf(OUT, "%s%s", e ? "," : "", gm->needed_by.a[e]);
            fputc('\n', OUT);
        }
    } else if (O.format == F_JSON) {
        fprintf(OUT, "{\n  \"schema\": 1,\n  \"bundle\": %s,\n  \"gems\": [", bundle ? "true" : "false");
        for (size_t g = 0; g < GEMS.n; g++) {
            gem_t *gm = &GEMS.a[g];
            fputs(g ? ",\n    { \"gem\": " : "\n    { \"gem\": ", OUT);
            json_str(OUT, gm->name);
            fputs(", \"groups\": ", OUT);
            json_str(OUT, gm->groups);
            fprintf(OUT, ", \"verdict\": \"%s\", \"evidence\": [", gm->verdict);
            for (size_t e = 0; e < gm->evidence.n; e++) {
                if (e) fputs(", ", OUT);
                json_str(OUT, gm->evidence.a[e]);
            }
            fputs("], \"needed_by\": [", OUT);
            for (size_t e = 0; e < gm->needed_by.n; e++) {
                if (e) fputs(", ", OUT);
                json_str(OUT, gm->needed_by.a[e]);
            }
            fputs("] }", OUT);
        }
        fputs(GEMS.n ? "\n  ]\n}\n" : "]\n}\n", OUT);
    } else {
        if (!O.quiet) {
            fprintf(OUT, "%sbackburn %s gems%s · %zu declared · ", C_BOLD, BACKBURN_VERSION, C_OFF, GEMS.n);
            if (bundle) fprintf(OUT, "read the source of %zu\n", nread);
            else fputs("no bundle: the app alone, gem constants guessed from gem names\n", OUT);
        }
        size_t w = 3;
        for (size_t g = 0; g < GEMS.n; g++)
            if (strlen(GEMS.a[g].name) > w) w = strlen(GEMS.a[g].name);
        static const char *order[] = {"unused", "tool", "auto", "used"};
        for (int o = 0; o < 4; o++) {
            size_t cnt = 0;
            for (size_t g = 0; g < GEMS.n; g++) cnt += !strcmp(GEMS.a[g].verdict, order[o]);
            if (!cnt) continue;
            fprintf(OUT, "\n%s%s (%zu)%s\n", C_BOLD, order[o], cnt, C_OFF);
            for (size_t g = 0; g < GEMS.n; g++) {
                gem_t *gm = &GEMS.a[g];
                if (strcmp(gm->verdict, order[o])) continue;
                fprintf(OUT, "  %-*s  %s%-18s%s ", (int) w, gm->name, C_DIM, gm->groups, C_OFF);
                for (size_t e = 0; e < gm->evidence.n; e++) fprintf(OUT, "%s%s", e ? "; " : "", gm->evidence.a[e]);
                if (gm->needed_by.n) {
                    fprintf(OUT, "%s  needed by ", gm->evidence.n ? "" : " ");
                    for (size_t e = 0; e < gm->needed_by.n && e < 4; e++) fprintf(OUT, "%s%s", e ? ", " : "", gm->needed_by.a[e]);
                    if (gm->needed_by.n > 4) fprintf(OUT, " and %zu more", gm->needed_by.n - 4);
                }
                fputc('\n', OUT);
            }
        }
    }
    free(gemfile);
    free(lock);
    return 0;
}
