/* Command-line parsing and how the config file fills in what the command line left open. */
#include <dirent.h>

#include "cli/cli.h"
#include "cli/options.h"
#include "cli/config.h"
#include "analysis/roots.h"

static const char USAGE[] =
    "usage: backburn [COMMAND] [OPTIONS] [PATH]\n"
    "\n"
    "Commands (default: scan):\n"
    "  scan            what code can no production path reach?\n"
    "                    --only REASONS  comma list of unreferenced, dead-chain, dead-cycle\n"
    "                    --test-refs include|exclude|only\n"
    "                    --write-baseline, --baseline FILE, --fail-on none|any|new\n"
    "  batches         in what independent pieces can I delete it?\n"
    "                    --max-lines N, --untested\n"
    "  explain NAME    why is this live, or why is it dead?  (Order#total, Admin::Report, file:line)\n"
    "                    --all-paths\n"
    "  roots           which entry points matched, and which patterns matched nothing?\n"
    "                    --unused\n"
    "  gems            which Gemfile gems does the app still need?\n"
    "                    --gem-src DIR\n"
    "  init            write .backburn.yml and .backburn.roots for this project\n"
    "                    --force\n"
    "\n"
    "Options:\n"
    "  --preset NAME     built-in roots: rails, graphql, sidekiq, phlex, pundit, devise, fast-mcp, thor, rails-framework (repeatable)\n"
    "  --roots FILE      an extra roots file, read after presets (repeatable)\n"
    "  --public-api      every public, documented method, class and constant is a root\n"
    "  --inflect         :line_items also reaches LineItem and LineItems\n"
    "  --exclude GLOB    skip matching paths (repeatable; default vendor/**, node_modules/**, tmp/**, log/**, coverage/**)\n"
    "  --tests GLOB      what counts as a test file (repeatable, replaces the default)\n"
    "  --format FMT      text, tsv, json, sarif\n"
    "  --output FILE     write the report to a file\n"
    "  --color WHEN      auto, always, never\n"
    "  --config FILE     read another config (default .backburn.yml); --no-config ignores it\n"
    "  -q, --quiet       no header or summary line in text\n"
    "  --version, --help\n"
    "\n"
    "Exit codes: 0 nothing to report under --fail-on, 1 dead code --fail-on counts,\n"
    "            2 usage error, 3 some Ruby files could not be parsed\n";

void usage_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("backburn: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputs("\n(see backburn --help)\n", stderr);
    va_end(ap);
    exit(2);
}

static void deprecated(const char *old, const char *new_) {
    fprintf(stderr, "backburn: %s is deprecated and will be removed in the next release; use %s\n", old, new_);
}

static int parse_reasons(const char *s) {
    int mask = 0;
    while (*s) {
        size_t n = strcspn(s, ",");
        if (n == 12 && !strncmp(s, "unreferenced", n)) mask |= 1 << R_NEVER;
        else if (n == 10 && !strncmp(s, "dead-chain", n)) mask |= 1 << R_DEAD_ONLY;
        else if (n == 10 && !strncmp(s, "dead-cycle", n)) mask |= 1 << R_CYCLE;
        else usage_error("--only takes unreferenced, dead-chain, dead-cycle; got %.*s", (int) n, s);
        s += n;
        if (*s == ',') s++;
    }
    return mask;
}

static int parse_format(const char *v) {
    if (!strcmp(v, "text")) return F_TEXT;
    if (!strcmp(v, "tsv")) return F_TSV;
    if (!strcmp(v, "json")) return F_JSON;
    if (!strcmp(v, "sarif")) return F_SARIF;
    usage_error("unknown format %s (text, tsv, json, sarif)", v);
    return F_TEXT;
}

static int parse_fail_on(const char *v) {
    if (!strcmp(v, "none")) return FAIL_NONE;
    if (!strcmp(v, "any")) return FAIL_ANY;
    if (!strcmp(v, "new")) return FAIL_NEW;
    usage_error("--fail-on takes none, any or new; got %s", v);
    return FAIL_NONE;
}

void parse_args(int argc, char **argv) {
    static const struct { const char *name; int cmd; } CMDS[] = {
        {"scan", CMD_SCAN}, {"batches", CMD_BATCHES}, {"explain", CMD_EXPLAIN}, {"roots", CMD_ROOTS},
        {"gems", CMD_GEMS}, {"init", CMD_INIT}, {NULL, 0}};
    int i = 1;
    O.cmd = CMD_SCAN;
    O.fail_on = FAIL_UNSET;
    O.color_when = "auto";
    if (argc > 1)
        for (int k = 0; CMDS[k].name; k++)
            if (!strcmp(argv[1], CMDS[k].name)) {
                O.cmd = CMDS[k].cmd;
                i = 2;
            }
    strvec pos = {0};
    int legacy_tsv = 0;
    for (; i < argc; i++) {
        char *a = argv[i];
        if (a[0] != '-' || !a[1]) {
            PUSH(pos, a);
            continue;
        }
        if (!strcmp(a, "--")) {
            for (i++; i < argc; i++) PUSH(pos, argv[i]);
            break;
        }
        char *val = NULL, *eq = NULL;
        if (a[1] == '-' && (eq = strchr(a, '='))) {
            *eq = 0;
            val = eq + 1;
        }
#define FLAG(s) (!strcmp(a, s))
#define VALUE()                                                                                  \
    (val ? val : (i + 1 < argc ? argv[++i] : (usage_error("%s needs a value", a), (char *) NULL)))
        if (FLAG("--help") || FLAG("-h")) {
            fputs(USAGE, stdout);
            exit(0);
        } else if (FLAG("--version")) {
            printf("backburn %s\n", BACKBURN_VERSION);
            exit(0);
        } else if (FLAG("--preset")) {
            PUSH(O.presets, VALUE());
            O.cli_presets = 1;
        } else if (FLAG("--roots")) {
            PUSH(O.roots, VALUE());
            O.cli_roots = 1;
        } else if (FLAG("--public-api")) {
            O.public_api = O.set_public_api = 1;
        } else if (FLAG("--inflect")) {
            O.inflect = O.set_inflect = 1;
        } else if (FLAG("--exclude")) {
            PUSH(O.exclude, VALUE());
            O.cli_exclude = 1;
        } else if (FLAG("--tests")) {
            PUSH(O.tests, VALUE());
            O.cli_tests = 1;
        } else if (FLAG("--format")) {
            O.format = parse_format(VALUE());
            O.set_format = 1;
        } else if (FLAG("--output") || FLAG("-o")) {
            O.output = VALUE();
        } else if (FLAG("--color") || FLAG("--colour")) {
            O.color_when = VALUE();
            if (strcmp(O.color_when, "auto") && strcmp(O.color_when, "always") && strcmp(O.color_when, "never"))
                usage_error("--color takes auto, always or never");
        } else if (FLAG("--config")) {
            O.config = VALUE();
        } else if (FLAG("--no-config")) {
            O.no_config = 1;
        } else if (FLAG("-q") || FLAG("--quiet")) {
            O.quiet = 1;
        } else if (FLAG("--only")) {
            O.only_mask = parse_reasons(VALUE());
        } else if (FLAG("--test-refs")) {
            const char *v = VALUE();
            if (!strcmp(v, "include")) O.test_refs = TR_INCLUDE;
            else if (!strcmp(v, "exclude")) O.test_refs = TR_EXCLUDE;
            else if (!strcmp(v, "only")) O.test_refs = TR_ONLY;
            else usage_error("--test-refs takes include, exclude or only");
        } else if (FLAG("--max-lines")) {
            const char *v = VALUE();
            char *end;
            long n = strtol(v, &end, 10);
            if (*end || n <= 0) usage_error("--max-lines takes a positive number");
            O.max_lines = (int) n;
        } else if (FLAG("--untested")) {
            O.untested = 1;
        } else if (FLAG("--all-paths")) {
            O.all_paths = 1;
        } else if (FLAG("--unused")) {
            O.unused_only = 1;
        } else if (FLAG("--gem-src")) {
            O.gem_src = VALUE();
        } else if (FLAG("--force")) {
            O.force = 1;
        } else if (FLAG("--write-baseline")) {
            O.write_baseline = 1;
        } else if (FLAG("--baseline")) {
            O.baseline = VALUE();
        } else if (FLAG("--fail-on")) {
            O.fail_on = parse_fail_on(VALUE());
            O.set_fail_on = 1;
        } else if (FLAG("--tsv")) {
            legacy_tsv = 1;
            deprecated("--tsv", "--format tsv");
        } else if (FLAG("--batches")) {
            O.cmd = CMD_BATCHES;
            O.legacy_batches = 1;
            deprecated("--batches", "backburn batches");
        } else {
            usage_error("unknown option %s", a);
        }
#undef FLAG
#undef VALUE
    }
    if (legacy_tsv && !O.set_format) O.format = F_LEGACY_TSV;
    if (O.cmd == CMD_EXPLAIN) {
        if (!pos.n) usage_error("explain needs a NAME, e.g. backburn explain Order#total");
        O.explain_name = pos.a[0];
        memmove(pos.a, pos.a + 1, (pos.n - 1) * sizeof *pos.a);
        pos.n--;
    }
    if (pos.n > 1) usage_error("expected one PATH, got %zu", pos.n);
    O.root = pos.n ? pos.a[0] : ".";
    free(pos.a);
}

void apply_config(void) {
    if (O.no_config) return;
    char *path = O.config ? xstrdup(O.config) : root_path(".backburn.yml");
    if (load_config(path, O.config != NULL) < 0) exit(2);
    free(path);
    const yent_t *y;
    if (!O.cli_presets && (y = cfg("presets")))
        for (size_t k = 0; k < y->vals.n; k++) PUSH(O.presets, y->vals.a[k]);
    if (!O.cli_roots && (y = cfg("roots")))
        for (size_t k = 0; k < y->vals.n; k++) PUSH(O.roots, root_path(y->vals.a[k]));
    if (!O.cli_exclude && (y = cfg("exclude")))
        for (size_t k = 0; k < y->vals.n; k++) PUSH(O.exclude, y->vals.a[k]);
    if (!O.cli_tests && (y = cfg("tests")))
        for (size_t k = 0; k < y->vals.n; k++) PUSH(O.tests, y->vals.a[k]);
    int b;
    if (!O.set_inflect && cfg_bool("inflect", &b)) O.inflect = b;
    if (!O.set_public_api && cfg_bool("public_api", &b)) O.public_api = b;
    if (!O.baseline && (y = cfg("baseline")) && y->vals.n) O.baseline = root_path(y->vals.a[0]);
    if (!O.set_format && (y = cfg("format")) && y->vals.n) O.format = parse_format(y->vals.a[0]);
    if (!O.set_fail_on && (y = cfg("fail_on")) && y->vals.n) O.fail_on = parse_fail_on(y->vals.a[0]);
}
