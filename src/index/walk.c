/* The file walk: which files are read, and the path globs that choose them. */
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

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

/* The kind a file's name gives it. A file with no extension is FK_SHEBANG: a worker reads
 * its first line, so the walk opens no files. */
static int kind_of_name(const char *name) {
    if (has_suffix(name, ".rb") || has_suffix(name, ".rake") || !strcmp(name, "Rakefile")) return FK_RUBY;
    if (has_suffix(name, ".erb") || has_suffix(name, ".tt")) return FK_TEMPLATE;
    if (has_suffix(name, ".haml")) return FK_HAML;
    if (has_suffix(name, ".slim")) return FK_SLIM;
    return strchr(name, '.') ? FK_NONE : FK_SHEBANG;
}

static VEC(file_job_t) TODO; /* the files to index, in walk order */

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

typedef struct {
    char *name;
    unsigned char type; /* d_type: DT_DIR and DT_REG save a stat */
} entry_t;

static int entry_cmp(const void *a, const void *b) {
    return strcmp(((const entry_t *) a)->name, ((const entry_t *) b)->name);
}

static void walk(sbuf *abs, sbuf *rel) {
    DIR *d = opendir(abs->s);
    if (!d) return;
    VEC(entry_t) names = {0};
    struct dirent *e;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) PUSH(names, ((entry_t){xstrdup(e->d_name), e->d_type}));
    closedir(d);
    qsort(names.a, names.n, sizeof *names.a, entry_cmp);
    size_t an = abs->n, rn = rel->n;
    for (size_t i = 0; i < names.n; i++) {
        const char *nm = names.a[i].name;
        sb_trunc(abs, an);
        sb_putc(abs, '/');
        sb_puts(abs, nm);
        sb_trunc(rel, rn);
        if (rn) sb_putc(rel, '/');
        sb_puts(rel, nm);
        /* symlinks and file systems that leave d_type unknown need a stat to tell */
        int is_dir = names.a[i].type == DT_DIR, is_reg = names.a[i].type == DT_REG, have_st = 0;
        struct stat st;
        if (!is_dir && !is_reg) {
            if (stat(abs->s, &st)) continue;
            have_st = 1;
            is_dir = S_ISDIR(st.st_mode);
            is_reg = S_ISREG(st.st_mode);
        }
        if (is_dir) {
            if (!strcmp(nm, ".git") || !strcmp(nm, ".bundle") || !strcmp(nm, ".backburn")) continue;
            sb_putc(rel, '/');
            int skip = any_glob(&O.exclude, rel->s);
            sb_trunc(rel, rel->n - 1);
            if (skip || (!have_st && stat(abs->s, &st)) || !dirset_add(st.st_dev, st.st_ino)) continue;
            walk(abs, rel);
        } else if (is_reg) {
            int k = kind_of_name(nm);
            if (!k || any_glob(&O.exclude, rel->s)) continue;
            PUSH(TODO, ((file_job_t){xstrdup(abs->s), xstrdup(rel->s), k}));
        }
    }
    sb_trunc(abs, an);
    sb_trunc(rel, rn);
    for (size_t i = 0; i < names.n; i++) free(names.a[i].name);
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
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    index_files(TODO.a, TODO.n, O.jobs > 0 ? O.jobs : cpus > 0 ? (int) cpus : 1);
    for (size_t i = 0; i < TODO.n; i++) free(TODO.a[i].abs), free(TODO.a[i].rel);
    free(TODO.a);
    TODO.a = NULL;
    TODO.n = TODO.cap = 0;
}
