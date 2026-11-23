/*
 * backburn: reports Ruby definitions that no production code can reach.
 *
 * It parses a codebase with Prism, builds a name-resolved reference graph,
 * marks everything reachable from a set of entry points (Trémaux's maze rule:
 * chalk each passage the first time, never walk a chalked one twice), and
 * lists what stays unmarked. Framework knowledge lives only in roots files
 * (presets compiled in, or project files), never in this code.
 *
 *   core/      helpers, the string interner, the definition tables, JSON
 *   index/     the file walk, ERB conversion and the Prism visitor
 *   analysis/  the reference graph, roots, marking, classification, batches
 *   cli/       options, the command line and the config file
 *   output/    reports and baselines
 *   commands/  scan, batches, explain, roots, gems, init
 */
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "analysis/analysis.h"
#include "analysis/roots.h"
#include "cli/cli.h"
#include "cli/options.h"
#include "commands/commands.h"
#include "index/indexer.h"

static void default_globs(void) {
    static const char *EXCLUDE[] = {"vendor/**", "node_modules/**", "tmp/**", "log/**", "coverage/**", NULL};
    static const char *TESTS[] = {"test/**", "spec/**", "**/*_test.rb", "**/*_spec.rb", NULL};
    if (!O.exclude.n)
        for (int k = 0; EXCLUDE[k]; k++) PUSH(O.exclude, (char *) EXCLUDE[k]);
    if (!O.tests.n)
        for (int k = 0; TESTS[k]; k++) PUSH(O.tests, (char *) TESTS[k]);
}

/* Opened only once the options and roots are known good, so a usage error
 * never truncates an existing report. */
static int open_output(void) {
    OUT = stdout;
    if (O.output && !(OUT = fopen(O.output, "w"))) {
        fprintf(stderr, "backburn: cannot write %s: %s\n", O.output, strerror(errno));
        return 0;
    }
    COLOR = !strcmp(O.color_when, "always") ||
            (!strcmp(O.color_when, "auto") && isatty(fileno(OUT)) && !getenv("NO_COLOR") && O.format == F_TEXT);
    return 1;
}

static int run(void) {
    switch (O.cmd) {
    case CMD_GEMS: return cmd_gems();
    case CMD_EXPLAIN: return cmd_explain();
    case CMD_ROOTS: return cmd_roots();
    default: return cmd_scan();
    }
}

int main(int argc, char **argv) {
    parse_args(argc, argv);
    if (O.cmd == CMD_INIT) {
        if (!open_output()) return 2;
        OPTS = pm_options_new();
        return cmd_init();
    }
    struct stat st;
    if (stat(O.root, &st) || !S_ISDIR(st.st_mode)) usage_error("%s is not a directory", O.root);
    apply_config();
    default_globs();
    load_all_rules();
    if (!open_output()) return 2;
    char *abs = realpath(O.root, NULL);
    ROOT_ABS = abs ? abs : xstrdup(O.root);

    analyse();
    compute_ids();
    int code = run();
    if (OUT != stdout) fclose(OUT);
    return code;
}
