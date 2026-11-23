/* The subcommands. Each runs after the analysis and returns the exit code. */
#ifndef BACKBURN_COMMANDS_COMMANDS_H
#define BACKBURN_COMMANDS_COMMANDS_H

int cmd_scan(void);    /* scan and batches: reports, baselines, --fail-on */
int cmd_explain(void); /* explain NAME */
int cmd_roots(void);   /* roots [--unused] */
int cmd_gems(void);    /* gems [--gem-src DIR] */
int cmd_init(void);    /* init [--force]: runs without an analysis */

#endif
