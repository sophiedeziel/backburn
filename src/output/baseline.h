/* Baselines: accept today's dead code so CI only fails on new dead code. */
#ifndef BACKBURN_OUTPUT_BASELINE_H
#define BACKBURN_OUTPUT_BASELINE_H

#include "core/util.h"

extern strvec BASELINE; /* sorted ids */
extern int HAVE_BASELINE;

int read_baseline(const char *path);
int in_baseline(const char *id);
int write_baseline_file(void);
char *git_head(void); /* short commit of the project, or NULL */

#endif
