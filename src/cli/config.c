/* Configuration: a flat YAML subset (scalars, [inline] lists and - block lists). */
#include <ctype.h>
#include <errno.h>

#include "cli/config.h"

static yent_vec CONFIG;

static char *yaml_scalar(const char *s, size_t n) {
    while (n && isspace((unsigned char) *s)) s++, n--;
    while (n && isspace((unsigned char) s[n - 1])) n--;
    if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') || (s[0] == '\'' && s[n - 1] == '\''))) s++, n -= 2;
    return xstrndup(s, n);
}

static size_t yaml_strip_comment(const char *s, size_t n) {
    char q = 0;
    for (size_t i = 0; i < n; i++) {
        if (q) {
            if (s[i] == q) q = 0;
        } else if (s[i] == '"' || s[i] == '\'') {
            q = s[i];
        } else if (s[i] == '#' && (i == 0 || isspace((unsigned char) s[i - 1]))) {
            return i;
        }
    }
    return n;
}

int load_config(const char *path, int required) {
    size_t n;
    char *text = read_file(path, &n);
    if (!text) {
        if (required) {
            fprintf(stderr, "backburn: cannot read config %s: %s\n", path, strerror(errno));
            return -1;
        }
        return 0;
    }
    yent_t *cur = NULL;
    int lineno = 0;
    for (char *p = text; *p;) {
        char *e = strchr(p, '\n');
        size_t len = e ? (size_t) (e - p) : strlen(p);
        lineno++;
        len = yaml_strip_comment(p, len);
        char *line = p;
        p = e ? e + 1 : p + strlen(p);
        size_t ind = 0;
        while (ind < len && line[ind] == ' ') ind++;
        if (ind == len) continue;
        if (!strncmp(line, "---", 3) && ind == 0) continue;
        if (line[ind] == '-' && (ind + 1 == len || line[ind + 1] == ' ')) {
            if (!cur) {
                fprintf(stderr, "backburn: %s:%d: list item without a key\n", path, lineno);
                free(text);
                return -1;
            }
            cur->is_list = 1;
            PUSH(cur->vals, yaml_scalar(line + ind + 1, len - ind - 1));
            continue;
        }
        char *colon = memchr(line, ':', len);
        if (!colon || ind) {
            fprintf(stderr, "backburn: %s:%d: expected `key: value`\n", path, lineno);
            free(text);
            return -1;
        }
        yent_t y;
        memset(&y, 0, sizeof y);
        y.key = yaml_scalar(line, (size_t) (colon - line));
        for (char *k = y.key; *k; k++)
            if (*k == '-') *k = '_';
        const char *v = colon + 1;
        size_t vn = len - (size_t) (v - line);
        while (vn && isspace((unsigned char) *v)) v++, vn--;
        while (vn && isspace((unsigned char) v[vn - 1])) vn--;
        if (vn && v[0] == '[') {
            y.is_list = 1;
            const char *q = v + 1, *end = v + vn;
            if (end > q && end[-1] == ']') end--;
            while (q < end) {
                const char *c = memchr(q, ',', (size_t) (end - q));
                if (!c) c = end;
                char *item = yaml_scalar(q, (size_t) (c - q));
                if (*item) PUSH(y.vals, item);
                else free(item);
                q = c + 1;
            }
        } else if (vn) {
            PUSH(y.vals, yaml_scalar(v, vn));
        }
        PUSH(CONFIG, y);
        cur = &CONFIG.a[CONFIG.n - 1];
    }
    free(text);
    return 1;
}

const yent_t *cfg(const char *key) {
    for (size_t i = 0; i < CONFIG.n; i++)
        if (!strcmp(CONFIG.a[i].key, key)) return &CONFIG.a[i];
    return NULL;
}

int cfg_bool(const char *key, int *out) {
    const yent_t *y = cfg(key);
    if (!y || !y->vals.n) return 0;
    const char *v = y->vals.a[0];
    *out = !strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "on") || !strcmp(v, "1");
    return 1;
}
