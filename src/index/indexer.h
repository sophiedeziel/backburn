/* The Prism visitor: turns one parsed file into definitions and named references. */
#ifndef BACKBURN_INDEX_INDEXER_H
#define BACKBURN_INDEX_INDEXER_H

#include "prism.h"

/* FK_SHEBANG: no extension, so Ruby only when its first line is a ruby or rails shebang. */
enum { FK_NONE, FK_RUBY, FK_TEMPLATE, FK_HAML, FK_SLIM, FK_SHEBANG };

extern pm_options_t *OPTS;

/* A file the walk chose: kind is FK_RUBY, a view converted to Ruby first
 * (FK_TEMPLATE for ERB and Thor .tt, FK_HAML, FK_SLIM), or FK_SHEBANG. */
typedef struct {
    char *abs, *rel;
    int kind;
} file_job_t;

/* Index the files in order. Up to jobs threads read and parse them ahead of
 * the main thread, which indexes each in turn, so the result is the same for
 * any number of jobs. */
void index_files(const file_job_t *files, size_t n, int jobs);

/* Prism helpers shared with the gems command. */
const pm_constant_t *safe_constant(pm_parser_t *p, pm_constant_id_t id);
const char *str_of(const pm_string_t *s, size_t *n);

#endif
