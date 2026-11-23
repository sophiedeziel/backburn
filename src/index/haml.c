/* HAML and Slim views to Ruby, line for line.
 *
 * Each output line holds the Ruby found on the same input line, so line
 * numbers still match. Only references matter, not behaviour, so control flow
 * is flattened: `- if x` becomes `x`, `- items.each do |i|` becomes
 * `items.each do |i| end`, and `- else` / `- end` vanish. Text keeps only its
 * #{…} interpolations; tag attributes keep only their Ruby values.
 */
#include <ctype.h>

#include "index/haml.h"

enum { BLOCK_NONE, BLOCK_SKIP, BLOCK_TEXT, BLOCK_RUBY };

/* Index just past the bracket that closes s[i] ( { [ (, honouring quotes. */
static size_t balanced(const char *s, size_t n, size_t i) {
    int depth = 0;
    char q = 0;
    for (; i < n; i++) {
        char c = s[i];
        if (q) {
            if (c == '\\') i++;
            else if (c == q) q = 0;
        } else if (c == '"' || c == '\'') {
            q = c;
        } else if (c == '(' || c == '{' || c == '[') {
            depth++;
        } else if (c == ')' || c == '}' || c == ']') {
            if (--depth == 0) return i + 1;
        }
    }
    return n;
}

/* Does the text leave a bracket open (so the next line continues it)? */
static int unbalanced(const char *s, size_t n) {
    int depth = 0;
    char q = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (q) {
            if (c == '\\') i++;
            else if (c == q) q = 0;
        } else if (c == '"' || c == '\'') {
            q = c;
        } else if (c == '(' || c == '{' || c == '[') {
            depth++;
        } else if (c == ')' || c == '}' || c == ']') {
            depth--;
        }
    }
    return depth > 0;
}

static void trim(const char **s, size_t *n) {
    while (*n && isspace((unsigned char) **s)) (*s)++, (*n)--;
    while (*n && isspace((unsigned char) (*s)[*n - 1])) (*n)--;
}

static int starts_word(const char *s, size_t n, const char *w) {
    size_t k = strlen(w);
    return n >= k && !memcmp(s, w, k) && (n == k || !(isalnum((unsigned char) s[k]) || s[k] == '_'));
}

/* Ends with `do` or `do |args|`: a block that needs an `end`. */
static int opens_do_block(const char *s, size_t n) {
    if (n && s[n - 1] == '|') {
        size_t k = n - 1;
        while (k && s[k - 1] != '|') k--;
        if (!k) return 0;
        n = k - 1;
        while (n && isspace((unsigned char) s[n - 1])) n--;
    }
    return n >= 2 && !memcmp(s + n - 2, "do", 2) && (n == 2 || !(isalnum((unsigned char) s[n - 3]) || s[n - 3] == '_'));
}

/* A Ruby statement, flattened. */
static void put_code(sbuf *o, const char *s, size_t n) {
    trim(&s, &n);
    if (!n) return;
    static const char *const COND[] = {"if", "unless", "elsif", "while", "until", "case", NULL};
    static const char *const DROP[] = {"else", "end", "begin", "ensure", "rescue", NULL};
    for (int k = 0; DROP[k]; k++)
        if (starts_word(s, n, DROP[k])) return;
    for (int k = 0; COND[k]; k++)
        if (starts_word(s, n, COND[k])) {
            s += strlen(COND[k]);
            n -= strlen(COND[k]);
            trim(&s, &n);
            break;
        }
    if (starts_word(s, n, "when")) {
        s += 4, n -= 4;
        trim(&s, &n);
        sb_putc(o, '[');
        sb_put(o, s, n);
        sb_puts(o, "] ; ");
        return;
    }
    if (!n) return;
    sb_put(o, s, n);
    if (opens_do_block(s, n)) sb_puts(o, " end");
    else if (s[n - 1] == '{' || (s[n - 1] == '|' && memchr(s, '{', n))) sb_puts(o, " }");
    sb_puts(o, " ; ");
}

/* Text: only its #{…} interpolations are Ruby. */
static void put_interp(sbuf *o, const char *s, size_t n) {
    for (size_t i = 0; i + 1 < n; i++) {
        if (s[i] == '#' && s[i + 1] == '{') {
            size_t e = balanced(s, n, i + 1);
            if (e > i + 3) put_code(o, s + i + 2, e - i - 3);
            i = e - 1;
        }
    }
}

/* A quoted attribute value is text; anything else is a Ruby expression. */
static size_t put_attr_value(sbuf *o, const char *s, size_t n, size_t i) {
    if (i >= n) return i;
    if (s[i] == '"' || s[i] == '\'') {
        size_t st = i++;
        while (i < n && s[i] != s[st]) i += s[i] == '\\' ? 2 : 1;
        if (i < n) i++;
        put_interp(o, s + st, i - st);
        return i;
    }
    size_t st = i;
    while (i < n && !isspace((unsigned char) s[i])) {
        if (s[i] == '(' || s[i] == '[' || s[i] == '{') i = balanced(s, n, i);
        else if (s[i] == '"' || s[i] == '\'') {
            char q = s[i++];
            while (i < n && s[i] != q) i += s[i] == '\\' ? 2 : 1;
            i++;
        } else i++;
    }
    if (i > n) i = n;
    put_code(o, s + st, i - st);
    return i;
}

/* name=value pairs, as in HAML (…) attributes and Slim attribute lists. */
static void put_html_attrs(sbuf *o, const char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        while (i < n && (isspace((unsigned char) s[i]) || s[i] == ',')) i++;
        size_t st = i;
        while (i < n && (isalnum((unsigned char) s[i]) || strchr("_-:@.", s[i]))) i++;
        if (i == st) {
            i++;
            continue;
        }
        if (i < n && s[i] == '=') i = put_attr_value(o, s, n, i + 1 + (i + 1 < n && s[i + 1] == '='));
    }
}

static size_t skip_tag_name(const char *s, size_t n, size_t i) {
    while (i < n && (isalnum((unsigned char) s[i]) || strchr("_-:%.#", s[i])) &&
           !(s[i] == '#' && i + 1 < n && s[i + 1] == '{') &&
           !(s[i] == ':' && (i + 1 >= n || s[i + 1] == ' '))) /* Slim's `li: a` nesting */
        i++;
    return i;
}

/* After a tag: = code, != code, == text, or plain text. */
static void put_tag_rest(sbuf *o, const char *s, size_t n, size_t i, int slim) {
    while (i < n && strchr("<>/", s[i])) i++;
    while (i < n && s[i] == ' ') i++;
    if (i >= n) return;
    if (s[i] == '=' && i + 1 < n && s[i + 1] == '=') put_interp(o, s + i + 2, n - i - 2);
    else if (s[i] == '=' || s[i] == '~') put_code(o, s + i + 1, n - i - 1);
    else if ((s[i] == '!' || s[i] == '&') && i + 1 < n && s[i + 1] == '=') put_code(o, s + i + 2, n - i - 2);
    else put_interp(o, s + i, n - i);
    (void) slim;
}

static int haml_line(sbuf *o, const char *s, size_t n) {
    switch (s[0]) {
    case '-':
        if (n > 1 && s[1] == '#') return BLOCK_SKIP;
        put_code(o, s + 1, n - 1);
        return BLOCK_NONE;
    case '=':
        if (n > 1 && s[1] == '=') put_interp(o, s + 2, n - 2);
        else put_code(o, s + 1, n - 1);
        return BLOCK_NONE;
    case '~':
        put_code(o, s + 1, n - 1);
        return BLOCK_NONE;
    case '!':
    case '&':
        if (n > 1 && (s[1] == '=' || s[1] == '~')) put_code(o, s + 2, n - 2);
        else put_interp(o, s, n);
        return BLOCK_NONE;
    case '/':
        return BLOCK_NONE; /* HTML comment: its nested lines still render */
    case '\\':
        put_interp(o, s + 1, n - 1);
        return BLOCK_NONE;
    case ':':
        return starts_word(s + 1, n - 1, "ruby") ? BLOCK_RUBY : BLOCK_TEXT;
    case '%':
    case '.':
    case '#':
        if (s[0] == '#' && n > 1 && s[1] == '{') break;
        {
            size_t i = skip_tag_name(s, n, 1);
            while (i < n && (s[i] == '{' || s[i] == '(' || s[i] == '[')) {
                size_t e = balanced(s, n, i);
                if (s[i] == '(') put_html_attrs(o, s + i + 1, e > i + 1 ? e - i - 2 : 0);
                else if (s[i] == '{') {
                    sb_putc(o, '(');
                    sb_put(o, s + i, e - i);
                    sb_puts(o, ") ; ");
                } else put_code(o, s + i + 1, e > i + 1 ? e - i - 2 : 0);
                i = e;
            }
            put_tag_rest(o, s, n, i, 0);
        }
        return BLOCK_NONE;
    }
    put_interp(o, s, n);
    return BLOCK_NONE;
}

static int slim_line(sbuf *o, const char *s, size_t n) {
    switch (s[0]) {
    case '/':
        return n > 1 && s[1] == '!' ? BLOCK_NONE : BLOCK_SKIP;
    case '|':
    case '\'':
        put_interp(o, s + 1, n - 1);
        return BLOCK_TEXT;
    case '<':
        put_interp(o, s, n);
        return BLOCK_NONE;
    case '-':
        put_code(o, s + 1, n - 1);
        return BLOCK_NONE;
    case '=': {
        size_t i = 1;
        while (i < n && strchr("=<>'", s[i])) i++;
        put_code(o, s + i, n - i);
        return BLOCK_NONE;
    }
    }
    if (starts_word(s, n, "doctype")) return BLOCK_NONE;
    /* filters: `ruby:`, `javascript:`, `markdown:` … on a line of their own */
    size_t w = 0;
    while (w < n && (isalnum((unsigned char) s[w]) || s[w] == '_' || s[w] == '-')) w++;
    if (w && w + 1 == n && s[w] == ':') return starts_word(s, n, "ruby") ? BLOCK_RUBY : BLOCK_TEXT;

    size_t i = skip_tag_name(s, n, 0);
    if (i < n && (s[i] == '(' || s[i] == '[' || s[i] == '{')) { /* wrapped attributes */
        size_t e = balanced(s, n, i);
        put_html_attrs(o, s + i + 1, e > i + 1 ? e - i - 2 : 0);
        i = e;
    } else {
        for (;;) { /* inline attributes: name=value … */
            size_t j = i;
            while (j < n && s[j] == ' ') j++;
            size_t st = j;
            while (j < n && (isalnum((unsigned char) s[j]) || strchr("_-:@", s[j]))) j++;
            if (j == st || j >= n || s[j] != '=' || (j + 1 < n && s[j + 1] == '=' && j == st)) break;
            i = put_attr_value(o, s, n, j + 1 + (j + 1 < n && s[j + 1] == '='));
        }
    }
    while (i < n && s[i] == ' ') i++;
    if (i < n && s[i] == ':' && i + 1 < n && s[i + 1] == ' ') { /* inline nesting: li: a href=x */
        const char *r = s + i + 1;
        size_t rn = n - i - 1;
        trim(&r, &rn);
        if (rn) slim_line(o, r, rn);
        return BLOCK_NONE;
    }
    if (i < n && s[i] == '=') {
        while (i < n && strchr("=<>'", s[i])) i++;
        put_code(o, s + i, n - i);
    } else if (i < n) {
        put_interp(o, s + i, n - i);
    }
    return BLOCK_NONE;
}

char *haml_to_ruby(const char *buf, size_t n, int slim, size_t *outn) {
    sbuf o = {0};
    sb_trunc(&o, 0);
    int block = BLOCK_NONE;
    size_t block_indent = 0;
    size_t pos = 0;
    while (pos < n) {
        const char *ls = buf + pos;
        const char *nl = memchr(ls, '\n', n - pos);
        size_t len = nl ? (size_t) (nl - ls) : n - pos;
        size_t lines = 1;
        /* a line that leaves a bracket open, or code ending in a comma, continues */
        sbuf joined = {0};
        const char *s = ls;
        size_t sn = len;
        while (nl && (unbalanced(s, sn) || (sn && s[sn - 1] == ',' && (s[strspn(s, " \t")] == '=' || s[strspn(s, " \t")] == '-' || s[strspn(s, " \t")] == '%')))) {
            const char *next = nl + 1;
            const char *nnl = memchr(next, '\n', (size_t) (buf + n - next));
            size_t nlen = nnl ? (size_t) (nnl - next) : (size_t) (buf + n - next);
            if (!joined.s) sb_put(&joined, s, sn);
            sb_putc(&joined, ' ');
            sb_put(&joined, next, nlen);
            s = joined.s;
            sn = joined.n;
            nl = nnl;
            lines++;
            if (lines > 50) break;
        }
        size_t indent = 0;
        while (indent < sn && (s[indent] == ' ' || s[indent] == '\t')) indent++;
        const char *t = s + indent;
        size_t tn = sn - indent;
        while (tn && isspace((unsigned char) t[tn - 1])) tn--;
        if (tn) {
            if (block != BLOCK_NONE && indent > block_indent) {
                if (block == BLOCK_RUBY) put_code(&o, t, tn);
                else if (block == BLOCK_TEXT) put_interp(&o, t, tn);
            } else {
                int b = slim ? slim_line(&o, t, tn) : haml_line(&o, t, tn);
                block = b;
                block_indent = indent;
            }
        }
        for (size_t k = 0; k < lines; k++) sb_putc(&o, '\n');
        free(joined.s);
        pos = nl ? (size_t) (nl - buf) + 1 : n;
    }
    *outn = o.n;
    return o.s;
}
