/* The Prism visitor: turns one parsed file into definitions and named references. */
#ifndef BACKBURN_INDEX_INDEXER_H
#define BACKBURN_INDEX_INDEXER_H

#include "prism.h"

enum { FK_NONE, FK_RUBY, FK_TEMPLATE, FK_HAML, FK_SLIM };

extern pm_options_t *OPTS;

/* Index one file: kind is FK_RUBY, or a view converted to Ruby first
 * (FK_TEMPLATE for ERB and Thor .tt, FK_HAML, FK_SLIM). */
void index_file(const char *abs, const char *rel, int kind);

/* Prism helpers shared with the gems command. */
const pm_constant_t *safe_constant(pm_parser_t *p, pm_constant_id_t id);
const char *str_of(const pm_string_t *s, size_t *n);

#endif
