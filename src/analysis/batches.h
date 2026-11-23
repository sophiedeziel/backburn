/* Removal batches: union-find over dead definitions, so each batch can be deleted on its own. */
#ifndef BACKBURN_ANALYSIS_BATCHES_H
#define BACKBURN_ANALYSIS_BATCHES_H

#include "core/model.h"

typedef struct {
    u32vec members, test_files;
    uint32_t lines, nfiles;
} batch_t;
typedef VEC(batch_t) batch_vec;

extern batch_vec BATCHES; /* sorted: untested first, then most lines */

void build_batches(void);

#endif
