/* ERB to Ruby, in place, keeping every newline so line numbers still match. */

#include "index/erb.h"

/* In place, keeping every newline: text outside tags becomes spaces, tag
 * openers become spaces, %> becomes " ;" so tags stay separate statements,
 * and <%# comments %> vanish. */
void erb_to_ruby(char *b, size_t n) {
    size_t i = 0;
    while (i < n) {
        if (b[i] == '<' && i + 1 < n && b[i + 1] == '%') {
            if (i + 2 < n && b[i + 2] == '%') { /* <%% is a literal <% */
                b[i] = b[i + 1] = b[i + 2] = ' ';
                i += 3;
                continue;
            }
            int comment = i + 2 < n && b[i + 2] == '#';
            b[i] = b[i + 1] = ' ';
            i += 2;
            while (!comment && i < n && (b[i] == '=' || b[i] == '-')) b[i++] = ' ';
            while (i < n) {
                if (b[i] == '%' && i + 1 < n && b[i + 1] == '>') {
                    if (comment) b[i] = b[i + 1] = ' ';
                    else {
                        if (i > 0 && b[i - 1] == '-') b[i - 1] = ' ';
                        b[i] = ' ';
                        b[i + 1] = ';';
                    }
                    i += 2;
                    break;
                }
                if (comment && b[i] != '\n') b[i] = ' ';
                i++;
            }
        } else {
            if (b[i] != '\n') b[i] = ' ';
            i++;
        }
    }
}
