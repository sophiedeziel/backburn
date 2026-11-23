/* Command-line parsing and how the config file fills in what the command line left open. */
#ifndef BACKBURN_CLI_CLI_H
#define BACKBURN_CLI_CLI_H

void parse_args(int argc, char **argv);
void apply_config(void);
void usage_error(const char *fmt, ...); /* prints, then exits 2 */

#endif
