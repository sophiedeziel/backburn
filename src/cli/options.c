/* Options, filled from the command line and then the config file. */

#include "cli/options.h"

options_t O;
FILE *OUT;

int COLOR;

char *root_path(const char *rel) {
    if (rel[0] == '/') return xstrdup(rel);
    sbuf b = {0};
    sb_puts(&b, O.root);
    if (b.n && b.s[b.n - 1] != '/') sb_putc(&b, '/');
    sb_puts(&b, rel);
    return b.s;
}
