/* The index: definitions, their references by name, and the side tables the visitor fills. */
#ifndef BACKBURN_CORE_MODEL_H
#define BACKBURN_CORE_MODEL_H

#include "core/util.h"

enum { K_LOAD, K_METHOD, K_SMETHOD, K_CLASS, K_MODULE, K_CONST };
enum { V_PUBLIC, V_PRIVATE, V_PROTECTED };
enum { R_LIVE, R_NEVER, R_DEAD_ONLY, R_CYCLE };
enum { VIA_CALLS, VIA_HOLDS };
#define NOT_ROOT INT32_MIN
enum { RR_LOAD = -2, RR_SUPER = -3, RR_OPERATOR = -4, RR_HOOK = -5, RR_PUBLIC_API = -6 };

/* An outgoing reference: a name, and whether it names a constant. */
typedef struct { uint32_t name, file, line; uint8_t is_const; } ref_t;

/* A definition. Index 0 is the synthetic <load time> definition that owns
 * every reference made outside a method body. */
typedef struct {
    uint8_t kind, vis, overrides, live, test_ref, reason, nodoc, via;
    uint8_t super_top; /* superclass written with a leading ::, resolved from the top level */
    uint32_t name, qual, file, line, end_line;
    int32_t super_, outer; /* classes: superclass as written, namespace written in */
    VEC(ref_t) refs;
    uint32_t in_live, in_dead;
    int32_t parent, root_rule, batch;
    uint32_t pfile, pline; /* where the parent names it, for explain */
} def_t;
typedef VEC(def_t) def_vec;

typedef struct { uint32_t name, file; uint8_t is_const; } tref_t;         /* test_refs + test_ref_files */
typedef struct { uint32_t cls, mod, outer; int32_t res; } incl_t;         /* include / prepend / extend */
typedef struct { uint32_t file, line; const char *what; } dyn_t;          /* reflective calls we can't read */
typedef struct { int32_t owner; uint32_t pat, file, line; uint8_t is_const; } pref_t; /* computed names */
typedef struct { uint32_t file; char *req; } req_t;                       /* require "x", for gems */
typedef struct { uint32_t file, line; char *msg; } perr_t;                /* files Prism rejected */
typedef VEC(tref_t) tref_vec;
typedef VEC(incl_t) incl_vec;
typedef VEC(dyn_t) dyn_vec;
typedef VEC(pref_t) pref_vec;
typedef VEC(req_t) req_vec;
typedef VEC(perr_t) perr_vec;

extern def_vec D;            /* every definition */
extern strvec FILES;         /* relative paths, in walk order */
extern u8vec FTEST, FTEMPLATE; /* per file: a test file; converted from ERB */
extern tref_vec TR;          /* references made from test files: they never mark anything */
extern incl_vec INC;         /* include / prepend / extend with a constant argument */
extern dyn_vec DYN;          /* reflective calls whose target can't be read */
extern pref_vec PREF;        /* computed names (:"visit_#{x}"), expanded after indexing */
extern req_vec REQ;          /* require "x", for the gems command */
extern perr_vec PERR;        /* Ruby files Prism rejected */
extern uint32_t TEMPLATE_ERRORS;

static inline int is_meth(int k) { return k == K_METHOD || k == K_SMETHOD; }
static inline int is_constk(int k) { return k == K_CLASS || k == K_MODULE || k == K_CONST; }
static inline const char *kind_word(int k) {
    switch (k) {
    case K_METHOD: case K_SMETHOD: return "method";
    case K_CLASS: return "class";
    case K_MODULE: return "module";
    case K_CONST: return "constant";
    default: return "load";
    }
}

#endif
