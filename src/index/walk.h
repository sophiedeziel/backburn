/* The file walk: which files are read, and the path globs that choose them. */
#ifndef BACKBURN_INDEX_WALK_H
#define BACKBURN_INDEX_WALK_H

#include "core/util.h"

/* A glob matches a relative path at any depth unless it starts with /. */
int path_glob(const char *glob, const char *path);
int any_glob(const strvec *globs, const char *path);

/* Walk root and index every Ruby, ERB and Ruby-script file in it. */
void index_tree(const char *root);

#endif
