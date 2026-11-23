/* Configuration: a flat YAML subset (scalars, [inline] lists and - block lists). */
#ifndef BACKBURN_CLI_CONFIG_H
#define BACKBURN_CLI_CONFIG_H

#include "core/util.h"

typedef struct { char *key; strvec vals; int is_list; } yent_t;
typedef VEC(yent_t) yent_vec;

int load_config(const char *path, int required); /* 1 read, 0 absent, -1 error */
const yent_t *cfg(const char *key);
int cfg_bool(const char *key, int *out);

#endif
