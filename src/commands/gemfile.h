/* Reading Gemfile and Gemfile.lock. */
#ifndef BACKBURN_COMMANDS_GEMFILE_H
#define BACKBURN_COMMANDS_GEMFILE_H

#include "core/util.h"

typedef struct {
    char *name, *groups, *path;
    int require_false, railtie, executable;
    u32vec tops;       /* top-level constants the gem defines */
    u32vec core_meths; /* methods it adds to core classes (String#red) */
    strvec evidence;
    strvec needed_by;
    const char *verdict;
} gem_t;
typedef VEC(gem_t) gem_vec;
/* Gemfile.lock: "    name (version)" opens a spec, "      dep" lists its deps. */
typedef struct { char *name; strvec deps; } lockspec_t;
typedef VEC(lockspec_t) lockspec_vec;

extern gem_vec GEMS;      /* gems the Gemfile declares */
extern lockspec_vec LOCK; /* every spec in Gemfile.lock and its dependencies */

int parse_gemfile(const char *path);
int parse_lock(const char *path);

#endif
