/* Options, filled from the command line and then the config file. */
#ifndef BACKBURN_CLI_OPTIONS_H
#define BACKBURN_CLI_OPTIONS_H

#include "core/util.h"

#include "version.h" /* BACKBURN_VERSION, generated from lib/backburn/version.rb */

enum { CMD_SCAN, CMD_BATCHES, CMD_EXPLAIN, CMD_ROOTS, CMD_GEMS, CMD_INIT };
enum { F_TEXT, F_TSV, F_JSON, F_SARIF, F_LEGACY_TSV };
enum { FAIL_UNSET = -1, FAIL_NONE, FAIL_ANY, FAIL_NEW };
enum { TR_INCLUDE, TR_EXCLUDE, TR_ONLY };

typedef struct {
    int cmd, format, quiet, color, inflect, public_api;
    int all_paths, unused_only, untested, force, write_baseline, no_config, legacy_batches;
    int max_lines, only_mask, test_refs, fail_on;
    int set_inflect, set_public_api, set_format, set_fail_on;
    const char *output, *config, *baseline, *gem_src, *root, *explain_name, *color_when;
    strvec presets, roots, exclude, tests;
    int cli_presets, cli_roots, cli_exclude, cli_tests;
} options_t;
extern options_t O;

extern FILE *OUT; /* where the report goes: stdout or --output */
extern int COLOR;
#define C_BOLD (COLOR ? "\033[1m" : "")
#define C_DIM (COLOR ? "\033[2m" : "")
#define C_YEL (COLOR ? "\033[33m" : "")
#define C_RED (COLOR ? "\033[31m" : "")
#define C_OFF (COLOR ? "\033[0m" : "")

char *root_path(const char *rel); /* a path relative to the project root (O.root) */

#endif
