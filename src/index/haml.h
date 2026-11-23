/* HAML and Slim views to Ruby, line for line. */
#ifndef BACKBURN_INDEX_HAML_H
#define BACKBURN_INDEX_HAML_H

#include "core/util.h"

/* Returns a new NUL-terminated buffer with the same number of lines as buf. */
char *haml_to_ruby(const char *buf, size_t n, int slim, size_t *outn);

#endif
