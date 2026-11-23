/* ERB to Ruby, in place, keeping every newline so line numbers still match. */
#ifndef BACKBURN_INDEX_ERB_H
#define BACKBURN_INDEX_ERB_H

#include <stddef.h>

void erb_to_ruby(char *b, size_t n);

#endif
