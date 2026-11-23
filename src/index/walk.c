/* The file walk: which files are read, and the path globs that choose them. */
#include <dirent.h>
#include <sys/stat.h>

#include "index/walk.h"
#include "core/glob.h"
#include "core/intern.h"
#include "core/model.h"
#include "cli/options.h"
#include "index/indexer.h"

/* A glob matches a relative path at any depth unless it starts with /.
 * Globs match like fnmatch without FNM_PATHNAME (core/glob.h), so * also
 * crosses /, and a leading double-star-slash may match nothing. */
static int glob_match_at(const char *g, const char *path) {
    if (glob_match_str(g, path)) return 1;
    if (!strncmp(g, "**/", 3) && glob_match_str(g + 3, path)) return 1;
    return 0;
}

int path_glob(const char *g, const char *path) {
    if (g[0] == '/') return glob_match_at(g + 1, path);
    if (glob_match_at(g, path)) return 1;
    for (const char *p = path; *p; p++)
        if (*p == '/' && p[1] && glob_match_at(g, p + 1)) return 1;
    return 0;
}

int any_glob(const strvec *gs, const char *path) {
    for (size_t i = 0; i < gs->n; i++)
        if (path_glob(gs->a[i], path)) return 1;
    return 0;
}

static int file_kind(const char *abs, const char *name) {
    if (has_suffix(name, ".rb") || has_suffix(name, ".rake") || !strcmp(name, "Rakefile")) return FK_RUBY;
    if (has_suffix(name, ".erb") || has_suffix(name, ".tt")) return FK_TEMPLATE;
    if (has_suffix(name, ".haml")) return FK_HAML;
    if (has_suffix(name, ".slim")) return FK_SLIM;
    if (!strchr(name, '.')) {
        FILE *f = fopen(abs, "rb");
        if (!f) return FK_NONE;
        char line[256];
        int ok = fgets(line, sizeof line, f) && line[0] == '#' && line[1] == '!' && (strstr(line, "ruby") || strstr(line, "rails"));
        fclose(f);
        return ok ? FK_RUBY : FK_NONE;
    }
    return FK_NONE;
}

/* (dev, inode) set so symlinked directories are walked once. */
typedef struct { uint64_t dev, ino; uint8_t used; } dirkey_t;
static struct { dirkey_t *k; size_t n, cap; } DIRSET;

static size_t dir_slot(uint64_t dev, uint64_t ino, size_t cap) {
    return (size_t) (((dev * 0x9E3779B97F4A7C15ULL) ^ ino) * 0x9E3779B97F4A7C15ULL >> 20) & (cap - 1);
}

/* Returns 0 when the directory (dev, inode) was already walked. */
static int dirset_add(dev_t dev, ino_t ino) {
    if ((DIRSET.n + 1) * 2 > DIRSET.cap) {
        size_t nc = DIRSET.cap ? DIRSET.cap * 2 : 256;
        dirkey_t *nk = xcalloc(nc, sizeof *nk);
        for (size_t i = 0; i < DIRSET.cap; i++)
            if (DIRSET.k[i].used) {
                size_t j = dir_slot(DIRSET.k[i].dev, DIRSET.k[i].ino, nc);
                while (nk[j].used) j = (j + 1) & (nc - 1);
                nk[j] = DIRSET.k[i];
            }
        free(DIRSET.k);
        DIRSET.k = nk;
        DIRSET.cap = nc;
    }
    size_t j = dir_slot((uint64_t) dev, (uint64_t) ino, DIRSET.cap);
    while (DIRSET.k[j].used) {
        if (DIRSET.k[j].dev == (uint64_t) dev && DIRSET.k[j].ino == (uint64_t) ino) return 0;
        j = (j + 1) & (DIRSET.cap - 1);
    }
    DIRSET.k[j] = (dirkey_t){(uint64_t) dev, (uint64_t) ino, 1};
    DIRSET.n++;
    return 1;
}

static void walk(sbuf *abs, sbuf *rel) {
    DIR *d = opendir(abs->s);
    if (!d) return;
    strvec names = {0};
    struct dirent *e;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) PUSH(names, xstrdup(e->d_name));
    closedir(d);
    qsort(names.a, names.n, sizeof *names.a, cmp_cstr);
    size_t an = abs->n, rn = rel->n;
    for (size_t i = 0; i < names.n; i++) {
        const char *nm = names.a[i];
        sb_trunc(abs, an);
        sb_putc(abs, '/');
        sb_puts(abs, nm);
        sb_trunc(rel, rn);
        if (rn) sb_putc(rel, '/');
        sb_puts(rel, nm);
        struct stat st;
        if (stat(abs->s, &st)) continue;
        if (S_ISDIR(st.st_mode)) {
            if (!strcmp(nm, ".git") || !strcmp(nm, ".bundle") || !strcmp(nm, ".backburn")) continue;
            sb_putc(rel, '/');
            int skip = any_glob(&O.exclude, rel->s);
            sb_trunc(rel, rel->n - 1);
            if (skip || !dirset_add(st.st_dev, st.st_ino)) continue;
            walk(abs, rel);
        } else if (S_ISREG(st.st_mode)) {
            if (any_glob(&O.exclude, rel->s)) continue;
            int k = file_kind(abs->s, nm);
            if (k) index_file(abs->s, rel->s, k);
        }
    }
    sb_trunc(abs, an);
    sb_trunc(rel, rn);
    for (size_t i = 0; i < names.n; i++) free(names.a[i]);
    free(names.a);
}

void index_tree(const char *root) {
    OPTS = pm_options_new();
    intern_words();
    def_t load;
    memset(&load, 0, sizeof load);
    load.kind = K_LOAD;
    load.name = load.qual = intern_cstr("<load time>");
    load.super_ = load.outer = -1;
    load.parent = -1;
    load.root_rule = NOT_ROOT;
    load.batch = -1;
    PUSH(D, load);
    sbuf abs = {0}, rel = {0};
    sb_puts(&abs, root);
    while (abs.n > 1 && abs.s[abs.n - 1] == '/') sb_trunc(&abs, abs.n - 1);
    sb_trunc(&rel, 0);
    struct stat st;
    if (!stat(abs.s, &st)) dirset_add(st.st_dev, st.st_ino);
    walk(&abs, &rel);
    free(abs.s);
    free(rel.s);
}
