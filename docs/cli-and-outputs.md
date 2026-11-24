# Backburn — CLI and outputs

Nov 23, 2026 · @Sophie

Backburn becomes a command with subcommands: `scan` reports dead code, `batches` plans its removal, `explain` answers why something is live or dead, and every command speaks text, TSV, JSON and SARIF with the same fields.

## Commands

```
backburn [COMMAND] [OPTIONS] [PATH]
```

`PATH` is the project root and defaults to the current directory. With no command, Backburn runs `scan`.

| Command | Answers | Its own options |
| --- | --- | --- |
| `scan` | What code can no production path reach? | `--only REASONS`, `--test-refs include\|exclude\|only` |
| `batches` | In what independent pieces can I delete it? | `--max-lines N`, `--untested` (only batches no test names) |
| `explain NAME` | Why is this live, or why is it dead? | `--all-paths` |
| `roots` | Which entry points matched, and which patterns matched nothing? | `--unused` (only patterns that matched nothing) |
| `gems` | Which Gemfile gems does the app still need? | `--gem-src DIR` |
| `init` | Write a starting config for this project | `--force` |

**`scan`** is today's report: every candidate with its reason, test tag and dead callers, then the uncertain definitions (what a computed `send` on self may call, never counted as candidates), then the dynamic dispatch sites that could hide callers. `--only` takes a comma list of `unreferenced`, `dead-chain`, `dead-cycle`. `--test-refs exclude` drops candidates that tests name; `--test-refs only` shows just those.

**`batches`** is today's `--batches`: dead code joined by references or containment, ordered untested first, then largest. Each batch lists its lines, files and the test files that name it.

**`explain NAME`** takes a qualified name (`Order#total`, `Admin::Report`, `Version.owned_by`). For a live definition it prints one path from a root, which is the question people ask when they expected something to be dead:

```
Order#price_of is live
  root   OrdersController#show       public action (< ActionController::Base)
  calls  Order#total                 app/controllers/orders_controller.rb:4
  calls  Order#price_of              app/models/order.rb:10
```

For a dead definition it prints its reason, its dead callers, and its batch. `--all-paths` lists every root that reaches it. This needs the marking step to keep one parent edge per definition, a 4-byte array.

**`roots`** prints each root rule with how many definitions it matched, so a roots file can be tuned: a pattern matching thousands is too broad, one matching nothing is stale.

**`gems`** is `gem_usage.rb` folded in. Run inside `bundle exec`, it reads every gem's source; outside a bundle it uses the app alone and says so in its header.

**`init`** detects Rails, GraphQL-ruby, Sidekiq, Phlex and Pundit from the Gemfile.lock and writes `.backburn.yml` with the matching roots files listed, plus an empty project roots file.

## Global options and configuration

Every command takes these. Command-line options win over the config file, which wins over built-in defaults.

| Option | Default | Effect |
| --- | --- | --- |
| `--preset NAME` | from `init` | built-in roots: `rails`, `graphql`, `sidekiq`, `phlex`, `pundit`, `rails-framework`; repeatable |
| `--roots FILE` | none | an extra roots file; repeatable, read after presets |
| `--public-api` | off | every public, documented method, class and constant is a root; `:nodoc:` marks internal |
| `--inflect` | off | `:line_items` also reaches `LineItem` and `LineItems` |
| `--exclude GLOB` | `vendor/**`, `node_modules/**`, `tmp/**`, `log/**`, `coverage/**` | skip matching paths; repeatable |
| `--tests GLOB` | `test/**`, `spec/**`, `**/*_test.rb`, `**/*_spec.rb` | what counts as a test file; repeatable, replaces the default |
| `--jobs N` | one per CPU | read and parse files on N threads (1 to 1024); one main thread indexes them in walk order, so the report is the same for any N |
| `--format FMT` | `text` | `text`, `tsv`, `json`, `sarif` |
| `--output FILE` | stdout | write the report to a file |
| `--color WHEN` | `auto` | `auto`, `always`, `never` |
| `--config FILE` | `.backburn.yml` | read another config; `--no-config` ignores it |
| `-q`, `--quiet` | off | no header or summary line in `text` |
| `--version`, `--help` |  |  |

The roots-file grammar stays as it is today (`Pattern`, `public Pattern`, `< Superclass`, `path:glob`). Presets are the same files, compiled into the binary so a single executable works anywhere.

**`.backburn.yml`**, at the project root. Backburn reads a small, flat subset of YAML: scalars and lists of strings, no anchors or nesting beyond one level. That keeps the binary free of a YAML library.

```yaml
presets: [rails, graphql]
roots: [.backburn.roots]      # project-specific patterns, committed
inflect: true
exclude: [tmp/**, log/**, db/migrate/**, script/**]   # replaces the default excludes
tests: [spec/**]
baseline: .backburn-baseline.json
```

`init` writes this file and an empty `.backburn.roots` with a comment header explaining the grammar, and leaves the project's `.gitignore` alone.

**What is committed and what isn't.** `.backburn.yml`, `.backburn.roots` and `.backburn-baseline.json` sit at the repository root and are committed: CI and everyone on the team need the same roots and baseline. `.backburn/` is local and never tracked: whichever command creates it first also writes `.backburn/.gitignore` containing `*`, which ignores the whole folder, that file included, without touching the project's own `.gitignore`. It holds the parse cache that makes reruns fast, the last report (`.backburn/last.json`, which `explain` reuses), and `--output` files when no path is given.

## Output formats

All four formats carry the same facts; only the shape changes. Every definition has a stable **id**, `kind:QualifiedName` (`method:Order#legacy_total`), so baselines and other tools survive line moves. When one name is defined in two files (a reopened class), the id gains `@path`.

**Reasons**, the same words everywhere:

| Reason | Meaning |
| --- | --- |
| `unreferenced` | nothing names it |
| `dead-chain` | only dead code reaches it |
| `dead-cycle` | it is reached only from a group that calls itself |

“Test-only” is a flag beside the reason (`tests: true`), never a reason of its own.

### text

For people. Grouped by reason for `scan`, by batch for `batches`; paths are relative to `PATH`; colour only on a terminal.

```
backburn 0.1 · 490 files · 1675 definitions · 80 ms · presets: rails, graphql
32 dead · 29 unreferenced · 3 dead-chain · 0 dead-cycle · 25 used only by tests

Unreferenced (29)
  app/graphql/base/node.rb:4            module  Base::Node
  app/models/print_queue/fit.rb:68      method  PrintQueue::Fit#cannot?
  app/models/print_queue/hold.rb:9      class   PrintQueue::Hold  [tests]
  …

Uncertain: a computed send may reach these (1)
  lib/dispatcher.rb:6                   method  Dispatcher#start  ← send(<computed>) lib/dispatcher.rb:3

Dynamic dispatch that may hide callers (11)
  app/models/gcode/slicer.rb:33         constantize(<computed>)
```

### tsv

One row per definition, a header row, tabs, no quoting. Columns, in order:

| Command | Columns |
| --- | --- |
| `scan` | `id`, `kind`, `name`, `file`, `line`, `end_line`, `reason`, `tests` (`0`/`1`), `batch` |
| `batches` | the same, sorted by batch, then reason, then location |
| `roots` | `rule`, `source` (preset or file:line), `matched` |
| `gems` | `gem`, `groups`, `verdict`, `evidence`, `needed_by` |

### json

For tools such as connascent and editors. One object; `schema` bumps on breaking changes.

```json
{
  "schema": 1,
  "tool": { "name": "backburn", "version": "0.1.0" },
  "project": { "root": "/path/to/app", "presets": ["rails", "graphql"], "git": "0ddf4da" },
  "stats": { "files": 490, "definitions": 1675, "references": 22849, "ms": 80 },
  "candidates": [
    {
      "id": "method:Order#old_rounding",
      "kind": "method",
      "name": "Order#old_rounding",
      "location": { "file": "app/models/order.rb", "line": 17, "end_line": 19 },
      "visibility": "public",
      "reason": "dead-chain",
      "tests": false,
      "test_files": [],
      "callers": ["method:Order#legacy_total"],
      "batch": 1
    }
  ],
  "batches": [
    { "id": 1, "lines": 6, "files": ["app/models/order.rb"], "definitions": ["method:Order#legacy_total", "method:Order#old_rounding"], "test_files": [] }
  ],
  "uncertain": [
    {
      "id": "method:Dispatcher#start",
      "kind": "method",
      "name": "Dispatcher#start",
      "location": { "file": "lib/dispatcher.rb", "line": 6, "end_line": 6 },
      "visibility": "public",
      "tests": false,
      "send": { "file": "lib/dispatcher.rb", "line": 3, "call": "send" }
    }
  ],
  "dynamic_sites": [
    { "file": "app/models/gcode/slicer.rb", "line": 33, "call": "constantize" }
  ]
}
```

`scan` and `batches` print this same document; `batches` is always filled in, so one JSON run serves both. `uncertain` lists definitions a computed send on self may reach, each with that send; they are never candidates. `explain` prints `{ "id", "live", "reason", "path": [{ "id", "via", "location" }] }`. For an uncertain definition, `live` is `false`, `reason` is `"uncertain"`, and the path step through the send has `"via": "send"` with the send's location.

### sarif

SARIF 2.1.0, so GitHub code scanning shows dead code as annotations on pull requests. One rule per reason (`backburn/unreferenced`, `backburn/dead-chain`, `backburn/dead-cycle`), level `note`. Each result carries the definition's location as its region, the batch as `properties.batch`, and the stable id as its `partialFingerprints.backburnId`, so GitHub tracks a finding across commits.

## Exit codes, baselines and CI

Backburn exits like a linter, so CI can gate on it.

| Code | Meaning |
| --- | --- |
| `0` | ran; nothing to report under `--fail-on` |
| `1` | ran; found dead code that `--fail-on` counts |
| `2` | usage error: unknown option, missing `PATH`, unreadable config or roots file |
| `3` | some Ruby files could not be parsed; the report still prints, and lists them |

`--fail-on` decides what counts:

- `none` (the default with no baseline): always exit `0`, for reading reports.
- `any`: any candidate fails.
- `new` (default when a baseline is set): only candidates whose id is not in the baseline fail.

**Baselines** make adoption on an old codebase possible: you accept today's dead code, and CI only stops new dead code. `backburn scan --write-baseline` writes `.backburn-baseline.json`, a sorted list of ids with the commit it was taken at. Ids, not line numbers, so moving code doesn't churn it. Removing a batch shrinks the baseline; `--write-baseline` rewrites it, and `scan` prints a hint when baseline entries no longer exist.

**CI recipe** for GitHub Actions:

```yaml
- run: backburn scan --format sarif --output backburn.sarif --fail-on new
- uses: github/codeql-action/upload-sarif@v3
  if: always()
  with:
    sarif_file: backburn.sarif
```

The job fails on new dead code, and the annotations show where it is.

## Migration and open questions

Today's flags map onto the new commands one to one; the old spellings keep working for one release with a deprecation line on stderr.

| Today | Proposed |
| --- | --- |
| `backburn DIR` | `backburn scan DIR` (or just `backburn DIR`) |
| `--roots rails.roots` | `--preset rails` (`--roots FILE` stays for project files) |
| `--batches` | `backburn batches` |
| `--tsv` | `--format tsv` |
| `never_referenced`, `only_dead_callers`, `dead_cycle` | `unreferenced`, `dead-chain`, `dead-cycle` |
| `only_in_tests` column | `tests` column |
| `ruby gem_usage.rb DIR` | `backburn gems DIR` |

**Open questions**

- [ ] **How does `gems` read the bundle from C?** Proposal: the binary runs `bundle exec ruby -e` once to print each gem's name and source path, then indexes those paths with Prism itself, as it does the app. That keeps one parser and drops `gem_usage.rb`.
- [ ] **How is it installed?** A gem named `backburn` with a native extension (compiled on `gem install`, ships the `backburn` executable) fits Ruby teams best; prebuilt binaries on GitHub releases are the fallback for CI images without a compiler.
- [ ] **Should `explain` take `file:line` as well as a name?** It's what an editor integration would send.
