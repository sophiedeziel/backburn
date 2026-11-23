# Backburn — technical spec for re-creating the dead-code finder

Nov 23, 2026 · @Sophie

## Purpose and scope

Backburn is a single C program that reports Ruby definitions no production code can reach. It parses a codebase with Prism, builds a name-resolved reference graph, marks everything reachable from a set of entry points, and lists what stays unmarked.

The algorithm is Trémaux's maze rule: walk every passage from the entrance, chalk each one the first time, never take a chalked passage twice. In graph terms that is a depth-first mark from the roots, which is the mark phase of a mark-and-sweep garbage collector. The name comes from firefighting: a backburn clears dead fuel in small controlled strips ahead of the fire, as Backburn's removal batches clear dead code one safe commit at a time.

The tool reports four kinds of dead code:

- **Never referenced:** nothing anywhere names it.
- **Only reached from dead code:** its only callers are themselves dead, at any depth.
- **Dead cycles:** a group that only calls itself (A calls B, B calls A).
- **Referenced only by tests:** tests are read, but they never keep code alive. Such candidates are tagged `[tests]`.

A reference counter misses the second and third kinds; reachability catches them in one pass.

**Design stance.** Every resolution is by name and deliberately conservative. A call `x.foo` reaches every method named `foo`. Errors are meant to be missed dead code, not false alarms. Where the tool cannot follow a call (computed `send`, `constantize`), it lists the site so a human can check before deleting.

**Non-goals.** No type inference, no runtime tracing, no automatic deletion. Framework knowledge lives in a plain-text roots file, never in the C code.

## Architecture

One pass indexes every file into definitions and named references; analysis then runs once over the whole graph. Only the roots file carries framework knowledge.

&#91;embedded content: Backburn pipeline · 8 steps, 1 input file\]

Everything lives in `backburn.c`, about 1,130 lines, in this order: helpers (`xrealloc`, a `VEC`/`PUSH` growable-array macro), the string interner, the definition tables, the Prism visitor with its `class_eval`-string and interpolated-name handling, ERB conversion and the RBS comment scan, the file walk, root rules, class and mixin resolution, name-pattern expansion, marking, namespace liveness, classification, removal batches, the reports, and `main`. Beside it: `Makefile`, `rails.roots`, `README.md`, `fixture/` with `expected.tsv`, project roots files under `samples/` (`rubygems.roots`, `rails-framework.roots`), and `gem_usage.rb`, a separate Ruby script that checks which Gemfile gems an app still uses. The planned command-line interface is specified in [Backburn — CLI and outputs](cli-and-outputs.md).

## Building against Prism

Link Prism's C library statically; no Ruby runtime is involved. Built and tested against Prism `main` at commit `a1d1d96` (November 2026). The C API changed in 2026 (arena allocation, visitor callbacks), so pin a commit and check the headers if a call below doesn't compile.

```
git clone --depth 1 https://github.com/ruby/prism vendor/prism
cd vendor/prism && ruby templates/template.rb && make build/libprism.a
```

If `gem fetch` is blocked, cloning from GitHub works; the templates step needs Ruby only at build time.

Makefile:

```
PRISM := vendor/prism
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation
backburn: backburn.c $(PRISM)/build/libprism.a
	$(CC) $(CFLAGS) -I$(PRISM)/include -o $@ backburn.c $(PRISM)/build/libprism.a -lm
test: backburn
	./backburn --roots rails.roots --tsv fixture | diff -u fixture/expected.tsv - && echo "fixture ok"
```

Prism calls used, per file:

| Purpose | Call |
| --- | --- |
| Set up and parse | `pm_arena_new()`, `pm_options_new()`, `pm_parser_new(arena, src, size, options)`, `pm_parse(parser)` |
| Tear down | `pm_parser_free`, `pm_options_free`, `pm_arena_free` |
| Walk the tree | `pm_visit_node(node, cb, data)`; the callback returns `PM_VISIT_DESCEND`, `PM_VISIT_SKIP` or `PM_VISIT_STOP` |
| Node type | `PM_NODE_TYPE(n)`, `PM_NODE_TYPE_P(n, PM_X_NODE)` |
| Names | `pm_parser_constant(parser, id)` then `pm_constant_start` / `pm_constant_length` |
| Source slices | `pm_parser_start(parser) + loc.start`, length `loc.length` (`pm_location_t` is offset + length) |
| Line numbers | `pm_line_offset_list_line_column(pm_parser_line_offsets(p), offset, pm_parser_start_line(p)).line` |
| String literals | `pm_string_source(&s)`, `pm_string_length(&s)` on a node's `unescaped` |
| Child lists | `pm_node_list_t {size, nodes}`, e.g. `call->arguments->arguments` |

Performance target: about 0.1 ms per file. rubygems.org (1700 files) indexes and analyses in 150–250 ms; Tentacles (490 files) in about 80 ms; the whole Rails repository (4237 files, 22,398 definitions) in about 0.55 s.

## Data model

Everything is a flat array of definitions plus, on each, a list of outgoing references by name. There are no pointers between definitions until analysis resolves names.

**String interner.** Every name (method name, constant segment, qualified name) is interned once: FNV-1a 64-bit hash, open addressing with linear probing, table doubled at 50% load, ids are dense `uint32_t`. Keep two lookups: `intern(s, n)` inserts, `find_name(s, n)` only looks. Analysis must use `find_name`, or repeated failed lookups grow the table and slow the run from \~150 ms to seconds.

**Definitions** (`def_t`), one array; index 0 is a synthetic `<load time>` definition that owns every reference made outside a method body.

| Field | Meaning |
| --- | --- |
| `kind` | `K_LOAD`, `K_METHOD`, `K_SMETHOD` (singleton/class method), `K_CLASS`, `K_MODULE`, `K_CONST` |
| `name` | interned short name: `total`, `Order`, `STATUSES` |
| `qual` | qualified name for reports and root patterns: `Order#total`, `Order.find_open`, `Admin::Report`, `Order::STATUSES` |
| `file`, `line` | index into the file list, 1-based line of the name |
| `vis` | `V_PUBLIC`, `V_PRIVATE`, `V_PROTECTED` |
| `refs` | outgoing references: `{name id, is_const}` |
| `super`, `outer` | classes only: superclass as written, and the `Module.nesting` outside the class, where Ruby looks the superclass up (for lexical resolution) |
| `overrides` | method body calls `super` |
| `live`, `root` | set by marking |
| `in_live`, `in_dead` | count of incoming references from live / dead definitions, set by classification |
| `test_ref` | some test file names it |
| `reason` | `R_LIVE`, `R_NEVER`, `R_DEAD_ONLY`, `R_CYCLE` |

Two more fields on every definition:

| Field | Meaning |
| --- | --- |
| `end_line` | last line of the definition (the `end` of a `def`, `class` or `module`, or of a constant's value), for batch line counts |
| `nodoc` | its line carries RDoc's `:nodoc:` comment, so `--public-api` treats it as internal |

**Other tables:**

- `test_refs`: references made from test files, kept apart; they never mark anything.
- `includes`: one record per `include` / `prepend` / `extend` with a constant argument: `{class qual, module as written, nesting inside the class, resolved module def}`.
- `dyn_sites`: `{file, line, what}` for every reflective call whose target can't be read, e.g. `send(<computed>)`. A send-like call on self (`send`, `__send__`, `public_send`, `try`, `try!`, `method`, `public_method`, with no receiver or `self.`) written inside a class or module also records `{self_ns, owner def, public_only, self_kind}` for the uncertain pass (Marking and classification). `self_kind` is the kind of the enclosing def (`K_METHOD` or `K_SMETHOD`), or unknown in a class body or a block outside any def.
- `files`: relative paths, in walk order.

Added for metaprogramming, batches and speed:

- `pattern_refs`: `{owner def, interned glob, is_const}` for every computed name such as `:"visit_#{x}"` (see Indexing rules). Expanded into ordinary references after indexing.
- `test_ref_files`: parallel to `test_refs`, the test file each test reference came from, so a batch can name the specs that mention it.
- Two indexes over `includes`, built once after resolution: by including class name, and by resolved module qualified name. Matching `< Pattern` roots without them scans every record per definition and took Rails from 0.6 s to 2.7 s.

**Ownership rule.** A reference belongs to the innermost enclosing method definition. Code in a class or module body, or at the top of a file, belongs to `<load time>`, which is always live. So `before_action :load_order` in a controller body keeps `load_order` alive even if the controller itself were dead. That is intentional: class bodies run when the file loads.

## Indexing rules

One visitor callback handles every node; anything not listed returns `PM_VISIT_DESCEND`. Visitor state (`ctx_t`): parser, file index, `is_test`, current namespace string `ns` (e.g. `Admin::Reports`), `nest` (the scopes of `Module.nesting`, innermost first, newline-separated and interned, or -1 at top level), `singleton` flag, `cur` (the def that owns references, 0 = load time), `cur_name` (enclosing method name, for `super`), current default visibility `vis`, and `next_def_vis` (visibility for the next `def` only, or -1). In test files, definitions are not recorded and references go to `test_refs`.

| Prism node | What it does |
| --- | --- |
| `ClassNode` | Visit the superclass expression first (it is a reference). Then enter the namespace (below) with kind `K_CLASS`. Return `SKIP`. |
| `ModuleNode` | Enter the namespace with kind `K_MODULE`. Return `SKIP`. |
| `SingletonClassNode` (`class << self`) | Set `singleton = 1`, reset visibility to public, visit the body, restore. Return `SKIP`. |
| `DefNode` | Qualified name = `ns` (or `Object` at top level) + `.` if singleton or receiver is `self`, else `#`, + name. Visibility = `next_def_vis` if set, else `vis`; then clear `next_def_vis`. Record a method def, set `cur` and `cur_name` to it, visit children, restore. Return `SKIP`. |
| `CallNode` | Always add a method reference to the call's name. Then the special cases below. Return `DESCEND`. |
| `SymbolNode` | Method reference to the symbol and to its setter (`:name` also reaches `name=`). With `--inflect`, also constant references to its camelized forms. |
| `StringNode` | If the string is constant-shaped (`Foo` or `Foo::Bar`, each segment starting A–Z), a constant reference to its last segment. |
| `ConstantReadNode`, `ConstantPathNode` | Constant reference to the node's own name (for `A::B` that is `B`; the visitor reaches `A` separately as the parent). |
| `ConstantWriteNode` | At load time only, outside tests: record a `K_CONST` def `ns::NAME`. |
| `SuperNode`, `ForwardingSuperNode` | Method reference to `cur_name`, and mark the current def `overrides = 1`. |

**Entering a namespace.** Take the constant path's source text, strip a leading `::`, and append it to `ns` with `::` (or use it alone at top level). Record the def with that qualified name. Set `nest` to that name followed by the outer `nest`: `module A; class B` has the nesting `A::B`, `A`, while `class A::B` at top level has only `A::B`. For classes, store `super` and `outer` (`nest` before entering): a constant or constant path is stored as its source text; a call like `ActiveRecord::Migration[7.1]` stores its receiver. Then save state, set `ns`, reset `singleton`, `vis` = public, `cur` = 0, visit the body, restore. Methods defined directly inside belong to the class, but references in the class body belong to load time.

**Call special cases** (only `CallNode`):

1. **Visibility.** A receiverless `private` / `protected` / `public` at load time: with no arguments, set `vis`; with a `def` as first argument (`private def x`), set `next_def_vis`.
2. **Def as argument.** For any other call, each argument that is a `DefNode` adds a method reference to that def's name. `register def self.handler(...)` passes the symbol `:handler`, so the handler is used. Skip this for `private`, `protected`, `public`, `private_class_method`, `public_class_method`, `module_function`.
3. **Mixins.** A receiverless `include` / `prepend` / `extend` inside a namespace, with constant arguments: push one `includes` record per argument, with the current `nest`.
4. **Reflection on methods:** `send`, `__send__`, `public_send`, `try`, `try!`, `method`, `public_method`, `instance_method`, `public_instance_method`, `respond_to?`, `alias_method`, `define_method`. First argument a string → method reference to it; a symbol is already handled by `SymbolNode`; anything else → record a dynamic site.
5. **Reflection on constants:** `const_get`, `constantize`, `safe_constantize`, `const_defined?`. String or symbol first argument → constant reference (last `::` segment). `"Foo".constantize` (string receiver) → constant reference. Any other receiver or argument → dynamic site.

**Metaprogramming rules.** Each of these came from a false positive on Rails, rubygems.org or Tentacles.

| Prism node or call | What it does |
| --- | --- |
| `InterpolatedStringNode`, `InterpolatedSymbolNode` | Build a glob from the parts: literal parts as written (escape `?` as `\?`), every interpolation as `*`, runs of `*` collapsed. Keep it only with at least 3 literal characters. If every literal character is `a–z`, `0–9`, `_`, `?`, `!` or `=`, it is a **method pattern** (`:"visit_#{name}"` → `visit_*`). If it holds an upper-case letter and only identifier characters and `::`, it is a **constant pattern** on its last segment (`"#{type}Policy"` → `*Policy`), which also needs 3 literal characters. Push a `pattern_refs` entry owned by the current def. A reflective call (`send`, `constantize`, …) whose argument yields a pattern records no dynamic site. |
| `StringNode` shaped exactly like a method name | 3–100 characters, starting `a–z` or `_`, then `a–z`, `0–9`, `_`, with an optional final `?`, `!` or `=`: a method reference. Covers `"download" => "download_uri"` followed by `send`, and `"_handle_action_missing"`. |
| `CallOperatorWriteNode`, `CallOrWriteNode`, `CallAndWriteNode` | `self.count += 1`, `obj.x \|\|= v`, `obj.x &&= v`: method references to both `read_name` and `write_name`. |
| `class_eval`, `module_eval`, `instance_eval`, `eval` with a string first argument | Rebuild the string's source (literal parts as written, each interpolation as the identifier `backburn_x`), parse it with a fresh Prism parser, and visit it with the same context, `in_eval` set. Under `in_eval`, record references (owned by the enclosing def) but no definitions, includes or dynamic sites. Nest at most 3 deep. |

**`:nodoc:`.** When recording a method, class, module or constant, scan the rest of its source line (up to 400 bytes) for `:nodoc:` and set `nodoc`.

**Expanding patterns**, after indexing and before marking: list every name that has method definitions and every name that has constant definitions. Sort `pattern_refs` by `(is_const, pattern)` and handle each distinct pattern once: compare the fixed prefix before the first `*` or `\` with `strncmp`, then `fnmatch`, and add an ordinary reference from each owner to every matching name. On Rails this turned 359 Arel and schema visitor methods live.

## Inputs

The tool walks one directory recursively (`opendir` / `stat`, following symlinks) and indexes files in walk order.

**Skipped directories:** `.git`, `vendor`, `node_modules`, `tmp`, `log`, `coverage`, `.bundle`.

**Files read:**

| File | How |
| --- | --- |
| `*.rb`, `*.rake`, `Rakefile` | parsed as Ruby |
| `*.erb` | converted to Ruby first (below), then parsed |
| Extensionless file whose first line starts `#!` and contains `ruby` | parsed as Ruby. Covers `bin/printer_monitor`, `bin/console`, Procfile scripts |

`*.tt` generator templates (Rails, Thor) are converted and parsed like `*.erb`: their `<%= %>` tags call generator helpers that nothing else names.

**ERB conversion**, in place on the byte buffer, preserving every newline so line numbers still match:

- Outside `<% … %>`, replace every byte except `\n` with a space.
- `<%`, `<%=`, `<%-` openers become spaces; the closing `%>` becomes ` ;  ` so consecutive tags stay separate statements; a trailing `-%>` dash becomes a space.
- `<%# … %>` comments are blanked entirely.

**Test files** are any path that starts with `test/` or `spec/`, contains `/test/` or `/spec/`, or ends `_test.rb` / `_spec.rb`. They are parsed with the same visitor, but record no definitions and send every reference to `test_refs`. Tests therefore never keep code alive; they only add the `[tests]` tag.

**Inline RBS comments.** Sorbet's inline RBS signatures live in comments, which Prism does not put in the tree, so scan the raw buffer after the visit. For every `#:` or `#|`, read to the end of the line and, for each token that starts with A–Z (not preceded by a letter, digit or `_`), add a constant reference for every `::`-separated segment, owned by load time. This keeps type aliases such as `TimeLike = T.type_alias { … }` alive when only `#: (at: TimeLike) -> void` uses them.

**Opt-in inflection (`--inflect`).** For each symbol made only of `a–z`, `0–9` and `_`, add constant references to its camelized form and simple singulars: `:line_items` → `LineItems`, `LineItem`; `:categories` → `Categories`, `Category`; `:boxes` → `Boxes`, `Boxe`, `Box`. This approximates `has_many` / `belongs_to` naming; it is cheap and only ever adds liveness.

## Roots

A definition is a root (an entrance to the maze) if any rule below matches. `<load time>` is always a root. Check in this order:

1. Its file matches a `path:` pattern.
2. It is a method with `overrides` set (it calls `super`, so whoever calls the parent version reaches it).
3. For classes, modules and constants: any non-`path:` pattern in the roots file matches (a ` public  ` prefix is ignored for these).
4. For methods:
   1. the name doesn't start with a letter or `_` (operators: `+`, `[]`, `<=>`, `==`…);
   2. the name is a Ruby hook (list below);
   3. `--public-api` is on, the method is public and its line has no `:nodoc:`;
   4. a roots-file pattern matches; a ` public  ` prefix restricts the pattern to public methods.

With `--public-api`, a class, module or constant without `:nodoc:` is also a root: a library's documented constants are its API as much as its methods. Rails marks its internals with `:nodoc:`, so this one rule took the Rails repository from 194 candidates to 139.

**Built-in hooks** (Ruby calls these by convention): `initialize`, `initialize_copy`, `initialize_dup`, `initialize_clone`, `method_missing`, `respond_to_missing?`, `respond_to?`, `to_s`, `to_str`, `inspect`, `to_proc`, `to_a`, `to_ary`, `to_h`, `to_hash`, `to_i`, `to_int`, `to_f`, `to_r`, `to_c`, `to_sym`, `to_json`, `as_json`, `to_param`, `call`, `each`, `hash`, `eql?`, `coerce`, `inherited`, `included`, `extended`, `prepended`, `method_added`, `singleton_method_added`, `const_missing`, `marshal_dump`, `marshal_load`, `_dump`, `_load`, `encode_with`, `init_with`, `pretty_print`, `freeze`, `dup`, `clone`, `===`, `==`, `<=>`.

**Roots file grammar.** One pattern per line. Blank lines are skipped, trailing spaces trimmed, and a line that is `#` or starts ` #  ` is a comment.

| Line | Matches |
| --- | --- |
| `Pattern` | `fnmatch(pattern, qual, 0)` on the qualified name. Without `FNM_PATHNAME`, `*` also matches `::`, so `*Controller` matches `Admin::UsersController` |
| `public Pattern` | the same, public methods only |
| `< Pattern` | everything in a class or module that *is a kind of* `Pattern` (below), and the class or module itself |
| `public < Pattern` | the same, public methods only |
| `path:glob` | every definition in a file whose relative path matches |

**“Is a kind of”.** For `< Pat`, take the definition's owner: the qualified name with the trailing `#name` or `.name` cut off (a class or module is its own owner). The owner matches if any of these holds:

- `fnmatch(Pat, owner)`;
- it **inherits** from `Pat`: walk the superclass chain from the owner's class def (at most 32 steps); at each step match `Pat` against the superclass as written and against the resolved class's qualified name; stop when a superclass doesn't resolve to an indexed class;
- it **mixes in** `Pat`: some `includes` record with this owner as the class has a module that matches `Pat`, as written or resolved;
- it is a **module included into a kind of `Pat`**: some `includes` record resolves to the owner and its class is a kind of `Pat` (the first three rules, applied to that class).

**Lexical resolution** of a name, the way Ruby looks a constant up: try `Scope::Name` for each scope of the recorded nesting, exactly and innermost first, then `Name` at the top level, against an index of class and module qualified names. A superclass is looked up in the nesting outside its class, a mixin in the nesting inside it, and a superclass written `::Base` at the top level only. Inside `module A; class B`, that tries `A::B::Name`, `A::Name`, `Name`; inside `class A::B` at top level, `A::B::Name` and `Name`, never `A::Name`. Ruby also searches the ancestors of the innermost scope; this lookup does not. When a class is reopened, keep the def that names a superclass. Resolve every `includes` record once, before marking; resolving inside the matching loop made a run take 3.3 s instead of 0.15 s.

**Namespaces are live through what they hold.** After marking, a class or module is live if a live class, module or constant is nested in it (`Erb` holds live generator classes, so `Erb` is live). Methods don't count: a method is live by its name alone, which says nothing about its class. On Tentacles, letting methods count wrongly revived 12 unwired `PrintQueue` classes whose methods share names like `change` with live code.

Why inheritance matters: on rubygems.org, name patterns alone left 1169 candidates, mostly Phlex components, Pundit policies, Avo resources, migrations and maintenance tasks. Adding `<` rules took it to 102, because those frameworks mark entry points by superclass or mixin, not by name.

## Marking and classification

After all files are indexed, analysis runs in five steps.

1. **Build name indexes.** Two arrays indexed by interned name id: `by_meth[name]` lists method defs (`K_METHOD`, `K_SMETHOD`), `by_const[name]` lists class, module and constant defs. A reference `{name, is_const}` targets `by_const[name]` or `by_meth[name]`: every definition with that name, in any class.
2. **Index classes and resolve mixins** (see Roots).
3. **Mark (Trémaux).** Push `<load time>` and every root on an explicit stack, marking each `live`. Pop a def; for each of its references, for each target not yet live, mark it live and push it. Each def is chalked once, so the walk is linear in definitions plus reference edges.
4. **Mark uncertain.** A fully computed send on self may call any method of the class it is written in, so those methods are not called dead. Seed a second walk from every live def that makes such a send (`dyn_sites` with `self_ns`): mark each still-dead method of `self_ns` as uncertain (`live = LIVE_MAYBE`), keeping only public ones for `public_send`, `try`, `try!` and `public_method`, and only instance methods (or only class methods) when `self_kind` says which. Then walk from uncertain defs exactly as in step 3, over what is still dead: their dead targets become uncertain, and so do the methods their own self-sends may reach. Sends in dead code start nothing, because dead code never runs. Last, a dead class or module holding an uncertain class, module or constant becomes uncertain, as in mark namespaces. Uncertain defs are truthy for every `live` test, so they never become candidates, batch members, baseline entries or `--fail-on` failures; the report lists them in their own section, each with the send that reaches it.
5. **Classify.**
   1. For every reference edge from `d` to `t` with `t ≠ d`, add 1 to `t.in_live` if `d` is live, else to `t.in_dead`.
   2. Build two flags arrays from `test_refs` (method names, constant names) and set `test_ref` on each def whose name appears there.
   3. For each def that is not live: `in_dead == 0` → `R_NEVER`; otherwise run a search from it over dead defs only. If it gets back to itself through another def, `R_CYCLE`; else `R_DEAD_ONLY`. Use a generation-stamped visited array so each search costs only the dead subgraph it touches.

The full order in `main` is: walk and index, build name indexes, **expand name patterns**, index classes and modules, resolve and index mixins, mark, **mark namespaces** (Roots), mark uncertain, classify, then report, or build batches with `--batches`.

A candidate is any def with `reason ≠ R_LIVE` and `reason ≠ R_UNCERTAIN`, except `<load time>`. The “referenced only by tests” count in the report is the number of candidates with `test_ref` set; it overlaps the other three groups rather than being a fourth exclusive group.

Two consequences worth keeping:

- Class liveness and method liveness are independent. A live class can hold dead methods; a dead class's methods can be live if their names are called elsewhere.
- Because calls match by name, one live call to `content` keeps every `content` method alive. Chains stop at the first common name. The output is precise about what it reports and incomplete about what it doesn't.

**Removal batches (`--batches`).** Group the dead definitions with union-find so each group can be deleted on its own:

1. Join every dead definition to each dead target of its references.
2. Join reopenings of the same dead class or module (same qualified name).
3. Join each dead method, constant, class or module to its owner when the owner is a dead class or module: the qualified name cut at the last `#` or `.` for methods, at the last `::` otherwise.
4. For each group, merge its `(file, line, end_line)` spans per file, so a class and its methods count once, and sum the lines. Count files.
5. Name the test files: every `test_refs` entry whose name and kind match a member contributes its `test_ref_files` entry.
6. Sort: groups no test file names first, then most lines first. Inside a group, list `unreferenced` members first, then by location.

No live code reaches a group and no reference crosses between groups, so deleting any one leaves the rest valid. Because test files are matched by name, groups with common names over-list specs (13 files for rubygems.org's `entry`/`content` group).

## Output and command line

```
backburn [--roots FILE] [--public-api] [--inflect] [--batches] [--tsv] DIR
```

| Flag | Effect |
| --- | --- |
| `--roots FILE` | load root patterns; layer project files by concatenating them with `rails.roots` |
| `--public-api` | every public method is a root (for gems and libraries) |
| `--inflect` | symbols also reach their camelized constant names |
| `--tsv` | machine output instead of the report |

`--batches` replaces the report with removal batches (Marking and classification) and combines with `--tsv`.

No `DIR` prints the usage line and exits 2. Candidates are sorted by file, then line.

**Human report**, in this order:

1. `Indexed N files: N definitions, N references, N test references in N ms` (timed with `clock()` from the walk to the end of classification).
2. `Dead code candidates: N of N definitions (N methods)`, then one line of counts: never referenced · only reached from dead code · in dead cycles · referenced only by tests.
3. One section per non-empty reason: `Never referenced (N)`, `Only reached from other dead code (N)`, `Dead cycles (they only call each other) (N)`. Each line: `file:line` padded to 48, kind padded to 8, qualified name, `   [tests] ` if tagged, and for the last two sections `  ← ` plus up to three dead callers' qualified names.
4. `Dynamic dispatch that may hide callers (N sites)`: the first 15 as `file:line  what(<computed>)`, then `… and N more`.

The `scan` subcommand adds an `Uncertain: a computed send may reach these (N)` section before the dynamic dispatch sites, and ` · N uncertain` to the counts line when N > 0. Each line: `file:line`, kind, qualified name, `[tests]` if tagged, and `← what(<computed>) file:line` for the send that reaches it.

Kind words: `method` (instance and singleton), `class`, `module`, `constant`.

**TSV**, one header line and one row per candidate, tab-separated:

| Column | Values |
| --- | --- |
| `file` | relative path |
| `line` | 1-based |
| `kind` | `method`, `class`, `module`, `constant` |
| `name` | qualified name |
| `reason` | `never_referenced`, `only_dead_callers`, `dead_cycle` |
| `only_in_tests` | `1` if a test names it, else `0` |

**Batch report.** After the `Indexed …` line: `N removal batches, N lines in all; N batches have no test references`. Then per batch a header `Batch N · N definitions · N lines · N files · no test references` (or ` tests:  ` and up to four test files, then `and N more`), and one line per member: `file:line-end_line` padded to 52, kind padded to 8, qualified name, `   (reached only from this batch) ` unless it is `unreferenced`, and `   [tests] ` when tests name it.

**Batch TSV** columns: `batch`, `file`, `line`, `end_line`, `kind`, `name`, `reason`, `only_in_tests`, sorted by batch, then members as in the report.

The redesign in [Backburn — CLI and outputs](cli-and-outputs.md) renames the reasons (`unreferenced`, `dead-chain`, `dead-cycle`) and adds JSON and SARIF; this section describes what the code does today.

## rails.roots in full

Ship this file next to the binary. It is the tool's only framework knowledge; every line was added because a real codebase (rubygems.org or Tentacles) needed it. Note `path:` globs also use `fnmatch` without `FNM_PATHNAME`, so `path:config/*` covers `config/initializers/x.rb`. Do not add `< ActiveRecord::Base`: it roots every model and hides the dead model methods the tool exists to find.

```
# Entry points the framework calls by convention. This file is the tool's only Rails knowledge.
#
#   Pattern               matches a qualified name: Class#instance_method, Class.class_method, Namespace::Class
#   public Pattern        only public methods
#   < Superclass          every class that inherits (directly or not) from Superclass, and all its methods
#   public < Superclass   ... and only its public methods
#   path:glob             everything defined in matching files

# --- Rails core
public < ActionController::Base
public < ActionController::API
public *Controller#*
public < ActionMailer::Base
< ActiveJob::Base
< ActiveRecord::Migration
< ActiveModel::*Validator
< Rails::Application
< Rails::Engine
*Helper
*Controller
*Job
*Mailer
path:db/migrate/*
path:db/seeds.rb
path:config/*
path:lib/tasks/*

# --- errors are raised by class name, rescued by class name; their methods are hooks
< StandardError
< *Error
< Exception

# --- view components (Phlex, ViewComponent)
< Phlex::*
< ViewComponent::Base
*Component#view_template
*Component#call
*Component#before_template
*Component#render?

# --- authorization (Pundit)
public < *ApplicationPolicy
public < *ApplicationPolicy::Scope

# --- admin (Avo) and maintenance tasks
< Avo::*
public < MaintenanceTasks::Task

# --- tests are roots for what they define (helpers, setup)
path:test/*
path:spec/*

# --- hooks Rails and common gems call by name
*.table_name_prefix
*#to_xml
*#as_json
*#serializable_hash
*.matches?
*#matches?
*#pundit_user
*#append_info_to_payload
*#search_data
< ActiveModel::Type::*
< ActiveRecord::Type::*
< RuboCop::Cop::*

# --- background workers marked by a mixin rather than a superclass
< Shoryuken::Worker
< Sidekiq::Job
< Sidekiq::Worker

# --- scripts are run by hand
path:script/*
path:bin/*

# --- Ruby's own protocols: IO-like and conversion objects are called by the standard library
*#readpartial
*#read
*#write
*#to_str
*#to_ary
*#to_proc
*#coerce
*#respond_to_missing?

# --- Action Cable
public < ActionCable::Channel::Base
< ActionCable::Connection::Base

# --- GraphQL-ruby: the schema calls types, mutations, resolvers and scalars by convention
public < GraphQL::Schema
public < GraphQL::Schema::*
< GraphQL::Dataloader::Source
*.resolve_type
*#resolve_type

# --- ActiveModel class-level hooks
*.human_attribute_name
*.model_name
```

Project-specific roots go in their own file, e.g. `rubygems.roots` with `ApplicationHelper#javascript_inline_importmap_tag` (an override of an importmap-rails helper, called from inside the gem), and are concatenated after this one.

For the Rails repository itself, run with `--public-api` plus `samples/rails-framework.roots`:

```
# Entry points of the Rails framework itself, layered on --public-api.
# Generators and commands are looked up by name (`rails generate model`, `rails server`),
# and Thor runs every public method of a generator in order.
< Rails::Generators::Base
< Rails::Command::Base
< Thor::Group
< Thor
# Bug-report templates and app templates are run as scripts.
path:guides/*
path:*/templates/*
```

## Tests and acceptance

A re-creation is done when `make test` passes on the fixture below and the three real codebases give the numbers in the last table. Each fixture file exercises one rule.

| File | Exercises |
| --- | --- |
| `app/controllers/application_controller.rb` | `< ActionController::Base` root through a superclass chain |
| `app/controllers/orders_controller.rb` | public actions live; private helper reached by a call; `send("format_#{…}")` recorded as a dynamic site |
| `app/helpers/orders_helper.rb` | a helper used only from ERB stays live |
| `app/models/order.rb` | symbol callback (`before_save :normalize`), dead chain, dead cycle, test-only method, unused constant |
| `app/views/orders/show.html.erb` | ERB code is read; ERB comments are not |
| `lib/legacy_report.rb` | an unreferenced class and its method |
| `lib/registry.rb` | `register def self.handle_csv` keeps the handler alive |
| `lib/workers/cleanup_worker.rb` | root by mixin (`include Shoryuken::Worker`); `super` makes `to_s` a root |
| `test/order_test.rb` | test references tag but don't keep alive |

```
# app/controllers/application_controller.rb
class ApplicationController < ActionController::Base
end

# app/controllers/orders_controller.rb
class OrdersController < ApplicationController
  def show
    @order = Order.new([])
    @total = @order.total
    render_badge
  end

  def preview
    send("format_#{params[:kind]}")
  end

  private

  def render_badge; end
  def unused_helper; end
end

# app/helpers/orders_helper.rb
module OrdersHelper
  def money(x)
    format("%.2f", x)
  end

  def unused_money(x)
    money(x * 100)
  end
end

# app/models/order.rb
class Order
  before_save :normalize
  STATUSES = %w[open closed].freeze

  def initialize(items)
    @items = items
  end

  def total
    @items.sum { |i| price_of(i) }
  end

  def legacy_total
    old_rounding(total)
  end

  def old_rounding(x)
    x.round(2)
  end

  def ping
    pong
  end

  def pong
    ping
  end

  def tested_only
    1
  end

  private

  def normalize; end
  def price_of(i) = i.price
  def dynamic_target; end
end

# app/views/orders/show.html.erb
<h1>Order</h1>
<% if @total %>
  <p><%= money(@total) %></p>
<% end %>
<%# legacy_total is mentioned only in this comment %>

# lib/legacy_report.rb
class LegacyReport
  def run
    puts "nobody calls me"
  end
end

# lib/registry.rb
module Registry
  HANDLERS = []
  def self.register(name) = HANDLERS << name

  # a def evaluates to its name, so this handler is referenced
  register def self.handle_csv(row) = row.split(",")

  # never registered, never called
  def self.handle_tsv(row) = row.split("\t")
end

# lib/workers/cleanup_worker.rb
class CleanupWorker
  include Shoryuken::Worker          # the queue library calls #perform: a root by mixin

  def perform(_msg, body)
    Trimmer.new.trim(body)
  end
end

class Trimmer < Struct.new(:x)
  def trim(s) = s.strip
  def to_s
    super                            # overrides a library method: a root
  end
  def unused_trim(s) = s.chomp
end

# test/order_test.rb
class OrderTest
  def test_tested_only
    assert_equal 1, Order.new([]).tested_only
  end
end
```

A tenth fixture file covers the metaprogramming rules: a computed `send` reaching every `visit_*` method, a method called only from a `class_eval` heredoc, and a compound write (`self.counter += 1`). Only `Visitor#leftover` should be reported.

```
# lib/visitor.rb
class Visitor
  def accept(node)
    bump
    send(:"visit_#{node.class.name}", node)   # computed name: reaches every visit_* method
  end

  def visit_Integer(n) = n
  def visit_String(s) = s

  class_eval <<~RUBY
    def generated = helper_from_eval           # code in a class_eval string is read
  RUBY

  def helper_from_eval = 1
  def leftover = 2                             # really dead

  private

  def bump = self.counter += 1                 # reads counter, writes counter=
  def counter = @counter.to_i
  def counter=(v)
    @counter = v
  end
end

Visitor.new.accept(1)
```

Expected `--roots rails.roots --tsv fixture` output (`fixture/expected.tsv`, tab-separated):

```
file	line	kind	name	reason	only_in_tests
app/controllers/orders_controller.rb	15	method	OrdersController#unused_helper	never_referenced	0
app/helpers/orders_helper.rb	6	method	OrdersHelper#unused_money	never_referenced	0
app/models/order.rb	3	constant	Order::STATUSES	never_referenced	0
app/models/order.rb	13	method	Order#legacy_total	never_referenced	0
app/models/order.rb	17	method	Order#old_rounding	only_dead_callers	0
app/models/order.rb	21	method	Order#ping	dead_cycle	0
app/models/order.rb	25	method	Order#pong	dead_cycle	0
app/models/order.rb	29	method	Order#tested_only	never_referenced	1
app/models/order.rb	37	method	Order#dynamic_target	never_referenced	0
lib/legacy_report.rb	1	class	LegacyReport	never_referenced	0
lib/legacy_report.rb	2	method	LegacyReport#run	never_referenced	0
lib/registry.rb	9	method	Registry.handle_tsv	never_referenced	0
lib/visitor.rb	15	method	Visitor#leftover	never_referenced	0
lib/workers/cleanup_worker.rb	14	method	Trimmer#unused_trim	never_referenced	0
```

`Registry` itself is no longer reported: it holds the live constant `HANDLERS`, so the namespace rule keeps it.

**Real codebases** (shallow clones; run with `--roots rails.roots --inflect`, rubygems.org with `rubygems.roots` appended):

| Codebase | Commit | Options | Files | Definitions | Candidates | Time | Hand check |
| --- | --- | --- | --- | --- | --- | --- | --- |
| rubygems/rubygems.org | `6f51e32` | `--roots rails.roots` + `rubygems.roots`, `--inflect` | 1700 | 4223 | 35 in 25 batches | 0.15–0.25 s | 35 correct; both earlier false positives (`Links#download_uri`, `Gemcutter::Markdown`) are gone |
| sophiedeziel/tentacles | `0ddf4da` | `--roots rails.roots`, `--inflect` | 490 | 1675 | 32 in 21 batches | 0.05–0.1 s | 32 correct: 1 probably dead, 3 small unused API, 16 not wired yet, 12 test double |
| rails/rails | `443726c` | `--public-api --roots samples/rails-framework.roots` | 4237 | 22,398 | 63 in 44 batches, plus 2 uncertain | 0.5–0.9 s | before the uncertain bucket, 65 in 46: 8 unreferenced, 24 internal and used only by tests, 21 test helpers shipped in `lib/`, 12 false positives (10 hooks other gems call, 2 code assembled in strings). The bucket moved `SchemaStatements#remove_constraint` (behind `send(command, …)`) and `MigrationContext#needs_migration?` (behind `public_send(direction, …)`) out of the candidates |

Smoke tests that the roots logic is doing the work: rubygems.org with no roots file gives 931 candidates, and Rails with no options gives 972.

## Known limits and next steps

The tool fails in two directions, and they need different fixes.

| Limit | Direction | Example |
| --- | --- | --- |
| Name collisions: a call reaches every method with that name | missed dead code | one live `.content` call keeps every `content` alive |
| Broad roots (`< Avo::*`, `path:config/*`) | missed dead code | dead code inside admin resources is never reported |
| Generated methods (`define_method`, associations, scopes, `enum`) have no `def` | missed dead code, never a false alarm | a generated `status` keeps a hand-written `status` alive |
| Computed names with no fixed text: `send(name)`, `public_send(a + b)`, `constantize` on a variable | false positives | the site is listed under dynamic dispatch; names with 3+ fixed characters are now matched as patterns; a send on self makes its class's dead methods uncertain instead of dead, but a send on another receiver, or one that reaches a superclass, a subclass or a mixin, still reports its targets as dead |
| Code assembled in strings outside `class_eval` (`batch << "def x"`, template handlers) | false positives | Rails' `TagBuilder#self_closing_tag_string`, `DelegationError.nil_target` |
| Methods another gem calls by convention, without `super` | false positives | Erubi/Herb `add_text`, Rack's `write_session`, Thor's `on_conflict_behavior` |
| Name-matched cycles | wrong bucket | `A#build` calls `X.build`, matched back to itself; reported as a cycle |
| “Not wired yet” looks the same as “abandoned” | needs another signal | the Tentacles queue operations merged one day before the run |

**Next steps, cheapest first:**

1. **Age as a feature.** Join each candidate and batch with `git log -1` for its lines. Dead plus recent plus tested means in progress; dead and untouched for months means delete. This is the input the ID3 triage idea needs.
2. **Production coverage as a cross-check.** Ruby's `Coverage.setup(oneshot_lines: true)` or Coverband records what actually ran. Dead in both is safe to delete; dead only here usually means hidden metaprogramming, which is how to grow the roots file.
3. **The local `.backburn/` folder** from [Backburn — CLI and outputs](cli-and-outputs.md): a parse cache and `.backburn/last.json` for `explain` to reuse. Every run parses from scratch today, in well under a second even on Rails, so this is the least pressing.

Done since the first version:

- String literals shaped like method names count as references, and interpolated names are matched as patterns.
- The CLI redesign in [Backburn — CLI and outputs](cli-and-outputs.md): subcommands, `explain`, JSON and SARIF, baselines, and presets compiled into the binary.
- `gem_usage.rb` folded into the binary as `backburn gems`.
- The uncertain bucket: methods a fully computed send on self may call, and dead code only they reach, are listed as uncertain instead of dead (Marking and classification, step 4).
