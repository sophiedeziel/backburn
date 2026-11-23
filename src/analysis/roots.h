/* Root rules: the entrances to the maze (presets, roots files, hooks, --public-api). */
#ifndef BACKBURN_ANALYSIS_ROOTS_H
#define BACKBURN_ANALYSIS_ROOTS_H

#include "core/glob.h"
#include "core/model.h"

enum { RULE_PAT, RULE_KIND, RULE_PATH };
typedef struct {
    int type, pub;
    char *pat, *text, *source; /* glob, the line as written, preset:line or file:line */
    glob_pat glob;             /* pat, compiled */
    uint32_t matched;
    uint8_t *memo;             /* RULE_KIND: per owner name id, 0 unknown, 1 yes, 2 no */
} rule_t;
typedef VEC(rule_t) rule_vec;

extern rule_vec RULES;
extern strvec ROOT_SOURCES;   /* labels of the loaded presets and files */
extern uint8_t *IS_HOOK;      /* per name id: a Ruby hook */
extern int32_t *FILE_PATH_RULE; /* per file: first matching path: rule, or -1 */

void load_all_rules(void);         /* presets, then roots files; exits 2 on error */
const char *preset_text(const char *name);
void prepare_roots(void);
int32_t root_for(uint32_t i);      /* rule code, or NOT_ROOT */
int rule_matches(rule_t *r, uint32_t i);
int is_operator(uint32_t name);
void rule_desc(int32_t rule, sbuf *b);

#endif
