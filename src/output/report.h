/* Reports for scan and batches: text, tsv, json, sarif (and the legacy tsv). */
#ifndef BACKBURN_OUTPUT_REPORT_H
#define BACKBURN_OUTPUT_REPORT_H

#include <stdio.h>
#include "analysis/batches.h"

extern u32vec CANDS; /* candidates after --only and --test-refs, by location */
extern u32vec UNCERTAIN; /* definitions a computed send on self may call, by location */

void select_candidates(void);
int batch_selected(const batch_t *b);

const char *reason_word(int reason);
const char *vis_word(int vis);
void json_id(FILE *f, uint32_t def);
void header_line(void);

void text_scan(void);
void text_batches(void);
void tsv_scan(void);
void tsv_batches(void);
void legacy_tsv_scan(void);
void legacy_tsv_batches(void);
void json_report(void);
void sarif_report(void);

#endif
