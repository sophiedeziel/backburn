# Backburn

Backburn finds Ruby code that no production path can reach. It parses your codebase with [Prism](https://github.com/ruby/prism), builds a name-resolved reference graph, marks everything reachable from your entry points ("roots": controller actions, jobs, migrations, and so on), and reports whatever stays unmarked. Because it walks reachability instead of counting references, it also catches **dead chains** (code called only by other dead code, at any depth) and **dead cycles** (methods that only call each other), which reference counters miss. Tests are read but never keep code alive; code that only tests use is tagged `[tests]`.

The algorithm is Trémaux's maze rule: walk every passage from the entrance and chalk it, and whatever stays unchalked is never visited. The name comes from firefighting. A backburn clears dead fuel in small, controlled strips, and Backburn's removal batches let you delete dead code one safe commit at a time.

Backburn only reports. It never deletes or edits your code.

## Build

Requirements:

- a C compiler (`cc`, clang or gcc) and `make`
- `git`, to fetch Prism
- Ruby, **at build time only**: Prism generates part of its C source from Ruby templates. The `backburn` binary itself needs no Ruby.

```sh
make        # fetches the pinned Prism commit into vendor/prism, builds libprism.a, then backburn
make test   # runs the checks in test/ (test/run.sh)
```

The first `make` clones Prism at the commit pinned in the `Makefile` (`PRISM_COMMIT`) and builds its static library. It then compiles `src/` into `build/` (object files plus the generated `build/presets.h`) and links `./backburn`. Prism's C API changed in 2026, so stick to that commit unless you also update the sources. The result is one self-contained executable with the presets compiled in. Copy it anywhere on your `PATH`. `make clean` removes the build output.

`make test` runs `test/run.sh`, which has 11 checks:

- 9 checks against `test/fixture/`:
  - the legacy `--tsv` output matches the spec's `expected.tsv`
  - `scan --format tsv` output
  - the five presets give `rails.roots`' result
  - `batches --format tsv` output
  - `explain` text
  - JSON and SARIF are valid (parsed with Ruby)
  - `--fail-on any` exits 1
  - an unknown option exits 2
- 3 regression cases in `test/cases/`:
  - `self_superclass`: `class Admin::Thing < ::Thing`
  - `haml_slim`: HAML and Slim views
  - `uncertain_send`: `send(name)` on self makes methods uncertain, not dead (scan, `explain` and JSON)

## Quick start

In the root of a Rails app:

```sh
backburn init         # picks presets from Gemfile.lock (and Gemfile, for thor)
backburn              # same as `backburn scan`: what can no production path reach?
backburn batches      # in what independent pieces can I delete it?
backburn explain Order#legacy_total   # why is this live, or why is it dead?
```

`init` writes two files that you should commit:

- `.backburn.yml` lists the presets that match your app and points to your project roots file. `init` detects Rails, GraphQL-ruby, Sidekiq, Phlex, Pundit, Devise and fast-mcp from `Gemfile.lock`. It adds `thor` only when your `Gemfile` declares it, because almost every Rails app has Thor in its lockfile through Rails itself:

  ```yaml
  # backburn configuration. Command-line options win over this file.
  presets: [rails, sidekiq, pundit]
  roots: [.backburn.roots]      # project-specific patterns, committed
  inflect: true
  # exclude: [db/migrate/**, script/**]
  # tests: [spec/**]
  # baseline: .backburn-baseline.json
  ```

- `.backburn.roots` is an empty roots file for your project's own entry points, with a comment header explaining the grammar.

`init` refuses to overwrite either file unless you pass `--force`. After `init`, the report header lists presets and roots files separately:

```
backburn 0.1.0 · 10 files · 43 definitions · 1 ms · presets: rails, sidekiq · roots: .backburn.roots
```

### What it looks like

The examples below are real output from the fixture in this repo (`./backburn --roots rails.roots --color never test/fixture`):

```
backburn 0.1.0 · 10 files · 43 definitions · 1 ms · roots: rails.roots
14 dead · 11 unreferenced · 1 dead-chain · 2 dead-cycle · 1 used only by tests

Unreferenced (11)
  app/controllers/orders_controller.rb:15  method   OrdersController#unused_helper
  app/helpers/orders_helper.rb:6           method   OrdersHelper#unused_money
  app/models/order.rb:3                    constant Order::STATUSES
  app/models/order.rb:13                   method   Order#legacy_total
  app/models/order.rb:29                   method   Order#tested_only  [tests]
  app/models/order.rb:37                   method   Order#dynamic_target
  lib/legacy_report.rb:1                   class    LegacyReport
  lib/legacy_report.rb:2                   method   LegacyReport#run
  lib/registry.rb:9                        method   Registry.handle_tsv
  lib/visitor.rb:15                        method   Visitor#leftover
  lib/workers/cleanup_worker.rb:14         method   Trimmer#unused_trim

Dead chain: only dead code reaches it (1)
  app/models/order.rb:17                   method   Order#old_rounding  ← Order#legacy_total

Dead cycle: reached only from code that calls itself (2)
  app/models/order.rb:21                   method   Order#ping  ← Order#pong
  app/models/order.rb:25                   method   Order#pong  ← Order#ping
```

`backburn batches` groups the same findings into pieces you can delete independently (output shortened):

```
backburn 0.1.0 · 10 files · 43 definitions · 1 ms · roots: rails.roots
11 removal batches · 29 lines in all · 10 have no test references

Batch 1 · 2 definitions · 6 lines · 1 file · no test references
  app/models/order.rb:13-15                            method   Order#legacy_total
  app/models/order.rb:17-19                            method   Order#old_rounding  (reached only from this batch)

Batch 2 · 2 definitions · 6 lines · 1 file · no test references
  app/models/order.rb:21-23                            method   Order#ping  (reached only from this batch)
  app/models/order.rb:25-27                            method   Order#pong  (reached only from this batch)

Batch 3 · 2 definitions · 5 lines · 1 file · no test references
  lib/legacy_report.rb:1-5                             class    LegacyReport
  lib/legacy_report.rb:2-4                             method   LegacyReport#run
...
Batch 11 · 1 definition · 3 lines · 1 file · tests: test/order_test.rb
  app/models/order.rb:29-31                            method   Order#tested_only  [tests]
```

`backburn explain` answers the follow-up question. For live code it prints one path from a root:

```
$ backburn explain Order#price_of --roots rails.roots test/fixture
Order#price_of is live  (app/models/order.rb:36)
  root   OrdersController#show  public < ActionController::Base  (rails.roots:10)
  calls  Order#total            app/controllers/orders_controller.rb:4
  calls  Order#price_of         app/models/order.rb:10
```

For dead code it prints the reason, the dead callers, and the batch it belongs to:

```
$ backburn explain Order#old_rounding --roots rails.roots test/fixture
Order#old_rounding is dead: dead-chain (only dead code reaches it)
  defined   app/models/order.rb:17-19  public method
  called by Order#legacy_total  app/models/order.rb:14 (dead)
  batch 1   2 definitions, 6 lines: Order#legacy_total, Order#old_rounding
```

`explain` accepts `Order#total` (instance method), `Order.find` (class method), `Admin::Report` (class, module or constant), or `app/models/order.rb:17` (`file:line`). With `--format json` it prints one object, or an array when the name matches several definitions.

## Commands

```
backburn [COMMAND] [OPTIONS] [PATH]
```

`PATH` is the project root and defaults to the current directory. With no command, Backburn runs `scan`.

| Command | Answers | Own options |
| --- | --- | --- |
| `scan` | What code can no production path reach? | `--only unreferenced,dead-chain,dead-cycle`, `--test-refs include\|exclude\|only`, `--write-baseline`, `--baseline FILE`, `--fail-on none\|any\|new` |
| `batches` | In what independent pieces can I delete it? | `--max-lines N`, `--untested` |
| `explain NAME` | Why is this live, or why is it dead? | `--all-paths` |
| `roots` | Which entry points matched, and which patterns matched nothing? | `--unused` |
| `gems` | Which Gemfile gems does the app still need? | `--gem-src DIR` |
| `init` | Write `.backburn.yml` and `.backburn.roots` for this project | `--force` |

Global options:

| Option | Effect |
| --- | --- |
| `--preset NAME` | built-in roots: `rails`, `graphql`, `sidekiq`, `phlex`, `pundit`, `devise`, `fast-mcp`, `thor`, `rails-framework` (repeatable) |
| `--roots FILE` | an extra roots file, read after presets (repeatable) |
| `--public-api` | every public, documented method, class and constant is a root |
| `--inflect` | `:line_items` also reaches `LineItem` and `LineItems` |
| `--exclude GLOB` | skip matching paths (repeatable; default `vendor/**`, `node_modules/**`, `tmp/**`, `log/**`, `coverage/**`) |
| `--tests GLOB` | what counts as a test file (repeatable, replaces the default) |
| `--format FMT` | `text`, `tsv`, `json`, `sarif` |
| `--output FILE` | write the report to a file |
| `--color WHEN` | `auto`, `always`, `never` |
| `--config FILE` | read another config (default `.backburn.yml`); `--no-config` ignores it |
| `-q`, `--quiet` | no header or summary line in text output |

Command-line options win over `.backburn.yml`, which wins over built-in defaults. All four formats carry the same facts. Every definition has a stable id such as `method:Order#legacy_total`, which survives line moves. See [docs/cli-and-outputs.md](docs/cli-and-outputs.md) for TSV columns, the JSON schema, SARIF details and the config file format.

## What it reads

Backburn reads the following files under `PATH`:

- `*.rb`, `*.rake` and `Rakefile`.
- Extensionless scripts whose `#!` line mentions `ruby` or `rails`.
- Views:
  - ERB (`*.erb`, and `*.tt` generator templates) is converted to Ruby with line numbers kept.
  - HAML (`*.haml`) and Slim (`*.slim`) are converted line by line. That covers `-` and `=` code lines, attribute values and `#{}` interpolation, and skips comments.
  - When a view template has Ruby that Backburn could only partly read, the text report ends with a dim note saying so.
- Old generator templates that are ERB inside a `.rb` file. These are retried as ERB and never count as tests.
- whenever's `runner "Foo.bar"` strings, which are read as Ruby.
- RSpec matchers `be_foo` and `have_foo`, which count as test references to `foo?` and `has_foo?`.

Test files (`test/`, `spec/`, `*_test.rb`, `*_spec.rb` by default) are read the same way, but they only tag candidates `[tests]`.

## Roots

A **root** is an entry point, meaning code that something outside your Ruby source calls: the router calls controller actions, Sidekiq calls `perform`, Rails runs migrations. Everything a root reaches is live. Everything else is a candidate. Backburn has no framework knowledge in its C code. All of it lives in roots files.

The following are always roots, without any configuration: code that runs at load time, methods that call `super` (whoever calls the parent reaches them), operators, and Ruby's own hooks (`initialize`, `method_missing`, `to_s`, `call`, `each`, `included`, `inherited`, and so on).

### Presets

Presets are roots files compiled into the binary (sources in `presets/`). Combine them freely:

```sh
backburn --preset rails --preset sidekiq --preset graphql
```

| Preset | Covers |
| --- | --- |
| `rails` | controllers, mailers, mailboxes (`< ActionMailbox::Base`), jobs, migrations, `config/`, `lib/tasks/`, errors, Action Cable, protocols Rails calls on your objects (`to_partial_path`, `table_name_prefix`/`_suffix`, `as_json`, ...), `bin/`, `script/` |
| `graphql` | GraphQL-ruby schema classes, types, mutations, resolvers, dataloader sources |
| `sidekiq` | workers marked by `Sidekiq::Job`, `Sidekiq::Worker`, `Shoryuken::Worker` |
| `phlex` | Phlex and ViewComponent components |
| `pundit` | Pundit policies and scopes |
| `devise` | Devise controllers and the hooks Devise calls on yours (`after_sign_in_path_for`, `configure_permitted_parameters`, ...) |
| `fast-mcp` | MCP tools and resources, which fast-mcp registers through `descendants` instead of by name |
| `thor` | every public method of a `Thor` or `Thor::Group` subclass |
| `rails-framework` | the Rails repository itself (generators, Thor commands); use with `--public-api` |

`rails.roots` at the repo root is the spec's original all-in-one file (Rails plus Sidekiq, GraphQL, Phlex and Pundit) and stays frozen as a reference. Conventions evolve in `presets/`. On the test fixture, `--roots rails.roots` and `--preset rails --preset sidekiq --preset graphql --preset phlex --preset pundit` give the same result.

The `rails` preset deliberately does not root `< ActiveRecord::Base`. That would keep every model method alive and hide the dead ones Backburn exists to find.

### Roots-file grammar

One pattern per line. Blank lines and lines starting with `#` are ignored. Patterns use shell globs (`fnmatch` without `FNM_PATHNAME`), so `*` also matches `::` and `/`.

| Line | Matches |
| --- | --- |
| `Pattern` | a qualified name: `Class#instance_method`, `Class.class_method`, `Namespace::Class` |
| `public Pattern` | the same, public methods only |
| `< Superclass` | every class or module that is a kind of `Superclass` (inherits from it or mixes it in), the class itself and everything in it |
| `public < Superclass` | the same, public methods only |
| `path:glob` | everything defined in files whose path (relative to the project root) matches |

Examples:

```
public < ActionController::Base      # every public action
*Job                                 # classes named like jobs
< Sidekiq::Job                       # include Sidekiq::Job counts as "a kind of"
ApplicationHelper#javascript_inline_importmap_tag   # a helper a gem calls for you
path:db/migrate/*
```

Put your project's own entry points in `.backburn.roots` (listed under `roots:` in `.backburn.yml`). For example, `samples/rubygems.roots` holds the single extra pattern rubygems.org needs.

To tune roots, run `backburn roots`. It prints each rule with how many definitions it matched. A rule that matches thousands of definitions is too broad, and `backburn roots --unused` lists the ones that matched nothing (stale or irrelevant):

```
$ backburn roots --roots rails.roots test/fixture
backburn 0.1.0 · 10 files · 43 definitions · 1 ms · roots: rails.roots
66 rules · 61 matched nothing

       4  public < ActionController::Base      rails.roots:10
       0  public < ActionController::API       rails.roots:11  (matched nothing)
       2  public *Controller#*                 rails.roots:12
...
```

### Gems and libraries: `--public-api`

For a gem, the entry points are its public API, not a framework's conventions. `--public-api` makes every public method, class, module and constant a root, unless its line carries `:nodoc:`. What remains is dead private and `:nodoc:` code. For the Rails repository itself, combine it with the `rails-framework` preset.

### Associations: `--inflect`

`has_many :line_items` names a class only through a symbol. With `--inflect`, every plain symbol also references its camelized singular and plural constants (`:line_items` reaches `LineItems` and `LineItem`). This can only make more code live, never less. `init` turns it on.

## Reading the report

Every candidate has exactly one reason:

| Reason | Meaning | What to do |
| --- | --- | --- |
| `unreferenced` | nothing anywhere names it | the simplest deletion |
| `dead-chain` | something calls it, but only dead code (the `←` names the dead callers) | delete it together with its callers |
| `dead-cycle` | reached only from a group that calls itself (`ping` calls `pong`, `pong` calls `ping`) | delete the whole group |

**`[tests]`** is a flag, not a reason. Tests name this code, but tests never keep code alive. Deleting it means deleting or updating those tests too. Use `scan --test-refs exclude` to hide such candidates, or `--test-refs only` to list just those.

**Dynamic dispatch that may hide callers.** When a call's target is computed (`send(name)`, `public_send(...)`, `constantize` on a variable), Backburn cannot follow it, so it lists the site. Check these before deleting anything nearby:

```
Dynamic dispatch that may hide callers (2)
  lib/dispatcher.rb:3                      public_send(<computed>)
  lib/dispatcher.rb:4                      constantize(<computed>)
```

Interpolated names with enough fixed text, such as `send("format_#{kind}")`, are matched as patterns (`format_*`) instead of being listed.

**Uncertain.** A fully computed `send(name)` on self (no receiver, or `self.`) may call any method of the class it is written in, so Backburn doesn't call those methods dead. It lists them, and any dead code only they reach, in their own section:

```
Uncertain: a computed send may reach these (2)
  lib/dispatcher.rb:6    method   Dispatcher#start  ← send(<computed>) lib/dispatcher.rb:3
  lib/exporter.rb:6      method   Exporter#csv  ← public_send(<computed>) lib/exporter.rb:3
```

`public_send`, `try` and `public_method` only reach public methods. A send in an instance method only reaches instance methods, and one in `def self.x` only reaches class methods. A send inside dead code makes nothing uncertain. Uncertain code is never a candidate: it stays out of batches, baselines and `--fail-on`. Before deleting one, check which names the send can receive; `backburn explain NAME` shows the path through the send.

**Batches.** `backburn batches` joins dead definitions connected by references or containment (a dead class and its methods, reopenings of the same class). No live code reaches a batch and no reference crosses between batches, so you can delete any one batch on its own and the rest stay valid. Batches are ordered untested first, then largest. Each batch is a natural size for one pull request. `--untested` shows only batches no test names, and `--max-lines N` caps their size.

## Gems

`backburn gems` checks which gems in your Gemfile the app still uses. When there is a `Gemfile.lock`, it runs `bundle exec ruby -e …` once to find each gem's source and indexes that source with Prism. With `--gem-src DIR` it reads gem sources from `DIR`. Without either, it falls back to the app alone and guesses each gem's constants from its name, and the report header says so.

Each gem gets a verdict:

| Verdict | Evidence |
| --- | --- |
| `used` | the app requires it, names a constant the gem owns, calls a method the gem adds to a core class, names it in a config file (`database.yml`, `.rubocop.yml`, `Procfile`, ...), or has a `config/<gem>.rb` |
| `auto` | it ships a Railtie or Engine, so Rails loads it without any call from the app |
| `tool` | it ships executables (run from the shell or CI, not from Ruby) |
| `unused` | none of the above |

`needed by` lists the gems that depend on it according to `Gemfile.lock`, which tells you whether removing it from the Gemfile would actually remove it from the bundle.

Treat the verdicts as hints, not proof. The matching is by name, and a test-only or CLI gem may be used in ways Backburn cannot see. An excerpt from a real Rails app:

```
backburn 0.1.0 gems · 37 declared · read the source of 36

unused (6)
  tzinfo-data                default
  bindex                     default               needed by web-console
  mini_racer                 default
...
tool (3)
  debug                      development,test   ships executables
  rubocop                    development,test   ships executables  needed by rubocop-rails, rubocop-rspec
...
auto (3)
  sprockets-rails            default            railtie: loads itself into Rails
...
used (25)
  mysql2                     default            named in config/database.yml
  puma                       default            configured in config/puma.rb
  redis                      default            constant Redis; named in config/cable.yml  needed by mock_redis
  sidekiq                    default            require sidekiq/web (config/routes.rb); constant Sidekiq; named in Procfile
...
```

## CI

Backburn exits like a linter:

| Code | Meaning |
| --- | --- |
| `0` | ran; nothing to report under `--fail-on` |
| `1` | ran; found dead code that `--fail-on` counts |
| `2` | usage error: unknown option, unknown preset, unreadable config or roots file, unknown `explain` name |
| `3` | some Ruby files could not be parsed; the report still prints and lists them |

`--fail-on` decides what counts:

- `none`: always exit 0. This is the default when there is no baseline.
- `any`: any candidate fails.
- `new`: only candidates missing from the baseline fail. This is the default when a baseline exists.

**Baselines** let you adopt Backburn on an existing codebase. You accept today's dead code, and CI blocks only new dead code:

```sh
backburn scan --write-baseline     # writes .backburn-baseline.json (sorted ids, commit)
```

Commit the file. Backburn picks up `.backburn-baseline.json` at the project root automatically whenever it exists. `baseline:` in `.backburn.yml` and `--baseline FILE` only change the path. With a baseline present, plain `backburn scan` exits 1 on new dead code and marks it `new` in the text report:

```
  lib/fresh.rb:1                           class    Fresh  new
  lib/fresh.rb:2                           method   Fresh#nobody  new
```

When baseline entries are no longer dead, `scan` prints a hint to rerun `--write-baseline` and shrink the file. The baseline stores ids, not line numbers, so moving code does not change it.

GitHub Actions, with dead code shown as annotations on pull requests:

```yaml
- run: backburn scan --format sarif --output backburn.sarif --fail-on new
- uses: github/codeql-action/upload-sarif@v3
  if: always()
  with:
    sarif_file: backburn.sarif
```

The SARIF output defines one rule per reason (`backburn/unreferenced`, `backburn/dead-chain`, `backburn/dead-cycle`) at level `note`, with the stable id as a fingerprint. GitHub uses the fingerprint to track a finding across commits.

## Limits

Backburn resolves every call by name and errs on the side of keeping code alive. Its findings are precise, but it misses some dead code. Know these limits before you delete anything:

- **Name-based matching.** A call `x.content` reaches every method named `content` in the codebase. One live `content` call keeps every `content` alive, so chains stop at the first common name. Generated methods (associations, scopes, `enum`, `define_method`) have no `def`, but their names still keep same-named hand-written methods alive.
- **Metaprogramming it can't follow.** Fully computed `send`/`public_send`/`constantize`, code assembled in strings outside `class_eval`, and methods another gem calls by convention without `super` all look dead. The first kind is listed under dynamic dispatch; when it is a send on self, the methods it may reach are listed as uncertain instead of dead. A send on another object, or one that reaches a superclass, subclass or mixin, still leaves its targets looking dead. The other kinds need a roots pattern.
- **"Not wired yet" looks like dead.** Code merged yesterday for a feature that isn't connected yet is indistinguishable from abandoned code. Check `git log` before deleting.
- **Broad roots hide dead code.** A rule like `< Avo::*` or `path:config/*` makes everything it matches live.
- **No type inference, no runtime tracing.** Cross-check with production coverage (Coverband, `Coverage.setup(oneshot_lines: true)`) before large deletions.

It never deletes anything. See [Known limits and next steps](docs/technical-spec.md#known-limits-and-next-steps) in the technical spec for the full table.

## Field notes

Backburn was run on 69 Ruby repositories (4,642 files, about 12,000 definitions). The first pass reported 238 candidates. Hand-checking them showed these main sources of false positives:

- HAML and Slim views that weren't read
- Devise hooks
- fast-mcp tools registered through `descendants`
- ActionMailbox
- entry points named only outside Ruby, such as `rails runner Repo.sync` in a crontab

After the fixes above, 167 candidates remain, and hand checks found most of them really dead or used only by tests. The crontab kind still needs a line in `.backburn.roots`.

## Status

**Acceptance.** On the spec's three real codebases, the binary reproduces the spec's file, definition, candidate and batch counts exactly. On Rails, the uncertain bucket moved 2 candidates (and 2 batches) out of the earlier 65 and 46:

| Codebase | Commit | Candidates | Batches | Time |
| --- | --- | --- | --- | --- |
| rubygems.org | `6f51e32` | 35 | 25 | about 0.15 s |
| Tentacles | `0ddf4da` | 32 | 21 | about 0.07 s |
| Rails | `443726c` | 63, plus 2 uncertain | 44 | about 0.38 s |

**Not implemented yet.** [docs/cli-and-outputs.md](docs/cli-and-outputs.md) describes a local `.backburn/` directory with a parse cache and `.backburn/last.json` for `explain` to reuse. Neither exists yet. Every run parses the project from scratch, which takes well under a second even on Rails.

## Legacy flags

The original flags still work for one release and print a deprecation warning on stderr:

| Old | New |
| --- | --- |
| `backburn --tsv DIR` | `backburn scan --format tsv DIR` |
| `backburn --batches DIR` | `backburn batches DIR` |
| `ruby gem_usage.rb DIR` | `backburn gems DIR` |

`gem_usage.rb` is now a shim that warns and runs `./backburn gems` from the same directory.

`--tsv` keeps the old column layout (`file`, `line`, `kind`, `name`, `reason`, `only_in_tests`) and old reason names (`never_referenced`, `only_dead_callers`, `dead_cycle`). `--format tsv` uses the new columns and reason names.

## Repository layout

| Path | What |
| --- | --- |
| `src/main.c` | entry point |
| `src/core/` | `util`, `intern` (string interner), `model` (definition tables), `json` |
| `src/index/` | `walk` (file walk, globs), `indexer` (Prism visitor), `erb`, `haml` (HAML/Slim to Ruby) |
| `src/analysis/` | `graph` (name indexes, pattern expansion, class and mixin resolution), `roots`, `mark` (marking, classification), `batches`, `analysis` (pipeline, stable ids) |
| `src/cli/` | `options`, `cli` (argument parsing), `config` (the YAML subset) |
| `src/output/` | `report` (text, TSV, JSON, SARIF), `baseline` |
| `src/commands/` | `scan`, `explain`, `roots`, `gems`, `gemfile`, `init`, `commands.h` |
| `presets/*.roots` | the built-in presets, compiled into the binary |
| `test/` | `run.sh` (what `make test` runs), `fixture/` (one rule per file, plus expected outputs), `cases/` (regression cases) |
| `docs/` | `technical-spec.md` (algorithm, rules, known limits), `cli-and-outputs.md` (CLI, formats, config, exit codes) |

Other top-level files:

- `Makefile` builds `src/` into `build/` and fetches Prism into `vendor/prism` (not committed). It also has the `test` and `clean` targets.
- `scripts/embed-presets.sh` generates `build/presets.h` from `presets/*.roots`.
- `rails.roots` is the spec's original roots file.
- `samples/` holds example project roots files.
- `gem_usage.rb` is a deprecated shim for `backburn gems`.

## License

Released under the [MIT License](LICENSE).
