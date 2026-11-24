/* init: write .backburn.yml and .backburn.roots for a project. */
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include "commands/commands.h"
#include "commands/gemfile.h"
#include "cli/options.h"

static const char ROOTS_HEADER[] =
    "# Project-specific entry points for backburn, read after the presets in .backburn.yml.\n"
    "# One pattern per line; blank lines and lines starting with # are ignored.\n"
    "#\n"
    "#   Pattern               a qualified name: Class#instance_method, Class.class_method, Namespace::Class\n"
    "#   public Pattern        the same, public methods only\n"
    "#   < Superclass          every class or module that is a kind of Superclass (inherits or mixes it in),\n"
    "#                         the class itself and everything in it\n"
    "#   public < Superclass   ... and only its public methods\n"
    "#   path:glob             everything defined in matching files (relative to the project root)\n"
    "#\n"
    "# Globs use fnmatch without FNM_PATHNAME, so * also matches :: and /.\n";

/* Never follows a symlink; without --force, never replaces a file. */
static FILE *create_file(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_NOFOLLOW | (O.force ? O_TRUNC : O_EXCL), 0644);
    return fd < 0 ? NULL : fdopen(fd, "w");
}

int cmd_init(void) {
    char *yml = root_path(".backburn.yml"), *roots = root_path(".backburn.roots");
    struct stat st; /* lstat: a dangling symlink still counts as there */
    if (!O.force && (lstat(yml, &st) == 0 || lstat(roots, &st) == 0)) {
        fprintf(stderr, "backburn: %s or %s already exists; use --force to overwrite\n", ".backburn.yml", ".backburn.roots");
        return 2;
    }
    char *lock = root_path("Gemfile.lock"), *gemfile = root_path("Gemfile");
    /* Frameworks count from the lockfile (rails pulls in railties); thor only
     * when the Gemfile declares it, since every Rails app has it as a dependency. */
    strvec names = {0};
    parse_gemfile(gemfile);
    if (parse_lock(lock)) {
        for (size_t i = 0; i < LOCK.n; i++)
            if (strcmp(LOCK.a[i].name, "thor")) PUSH(names, LOCK.a[i].name);
    }
    for (size_t i = 0; i < GEMS.n; i++) PUSH(names, GEMS.a[i].name);
    static const struct { const char *gem, *preset; } MAP[] = {
        {"rails", "rails"}, {"railties", "rails"}, {"graphql", "graphql"}, {"sidekiq", "sidekiq"},
        {"shoryuken", "sidekiq"}, {"phlex", "phlex"}, {"phlex-rails", "phlex"}, {"view_component", "phlex"},
        {"pundit", "pundit"}, {"devise", "devise"}, {"fast-mcp", "fast-mcp"}, {"thor", "thor"}, {NULL, NULL}};
    static const char *ORDER[] = {"rails", "graphql", "sidekiq", "phlex", "pundit", "devise", "fast-mcp", "thor", NULL};
    int on[8] = {0};
    for (size_t i = 0; i < names.n; i++)
        for (int m = 0; MAP[m].gem; m++)
            if (!strcmp(names.a[i], MAP[m].gem))
                for (int o = 0; ORDER[o]; o++)
                    if (!strcmp(ORDER[o], MAP[m].preset)) on[o] = 1;
    FILE *f = create_file(yml);
    if (!f) {
        fprintf(stderr, "backburn: cannot write %s: %s\n", yml, strerror(errno));
        return 2;
    }
    fputs("# backburn configuration. Command-line options win over this file.\n", f);
    fputs("presets: [", f);
    int n = 0;
    for (int o = 0; ORDER[o]; o++)
        if (on[o]) fprintf(f, "%s%s", n++ ? ", " : "", ORDER[o]);
    fputs("]\nroots: [.backburn.roots]      # project-specific patterns, committed\n", f);
    fputs("inflect: true\n", f);
    fputs("# exclude: [tmp/**, log/**, db/migrate/**, script/**]   # replaces the default excludes\n", f);
    fputs("# tests: [spec/**]\n", f);
    fputs("# baseline: .backburn-baseline.json\n", f);
    fclose(f);
    f = create_file(roots);
    if (!f) {
        fprintf(stderr, "backburn: cannot write %s: %s\n", roots, strerror(errno));
        return 2;
    }
    fputs(ROOTS_HEADER, f);
    fclose(f);
    fprintf(OUT, "wrote .backburn.yml (presets: ");
    n = 0;
    for (int o = 0; ORDER[o]; o++)
        if (on[o]) fprintf(OUT, "%s%s", n++ ? ", " : "", ORDER[o]);
    fprintf(OUT, "%s) and .backburn.roots\n", n ? "" : "none detected");
    free(yml);
    free(roots);
    free(lock);
    free(gemfile);
    free(names.a);
    return 0;
}
