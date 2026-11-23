/* Marking (Trémaux), namespace liveness, and classification of what stays dead. */
#ifndef BACKBURN_ANALYSIS_MARK_H
#define BACKBURN_ANALYSIS_MARK_H

#include "analysis/graph.h"

extern csr_t TR_BY_KEY, DEAD_CALLERS; /* by reference key: test references; dead defs naming it */
extern size_t NCAND;

void mark(void);            /* depth-first mark from <load time> and every root */
void mark_namespaces(void); /* a class is live when it holds a live class, module or constant */
void mark_uncertain(void);  /* what a computed send on self may call: live == LIVE_MAYBE */
void classify(void);        /* unreferenced, dead-chain or dead-cycle; uncertain for LIVE_MAYBE */

/* For an uncertain def: the DYN index of the send that reaches it, or -1. */
int32_t uncertain_site(uint32_t t);

void dead_callers(uint32_t t, u32vec *out);
void test_files_of(uint32_t t, u32vec *out);

#endif
