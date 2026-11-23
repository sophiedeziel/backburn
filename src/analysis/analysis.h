/* The analysis pipeline, definition ordering, and stable ids. */
#ifndef BACKBURN_ANALYSIS_ANALYSIS_H
#define BACKBURN_ANALYSIS_ANALYSIS_H

#include "core/util.h"

extern char *ROOT_ABS;
extern double ELAPSED_MS;
extern size_t NREFS;
extern uint32_t *FILE_RANK; /* file index -> rank by path */

/* Walk and index, build name indexes, expand name patterns, index classes,
 * resolve mixins, mark, mark namespaces, mark what computed sends on
 * self may call (uncertain), classify, build batches. */
void analyse(void);

int def_loc_cmp(uint32_t a, uint32_t b); /* by file path, then line */
int def_loc_qcmp(const void *a, const void *b);

/* Stable ids: kind:QualifiedName, plus @path when one name lives in two files. */
void compute_ids(void);
void def_id(sbuf *b, uint32_t i);

#endif
