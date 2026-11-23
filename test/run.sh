#!/bin/sh
# Acceptance checks for backburn, run by `make test`.
# The fixture under test/fixture exercises one rule per file (see docs/technical-spec.md, "Tests and acceptance").
set -u
cd "$(dirname "$0")/.."
BIN=${BACKBURN:-./backburn}
FX=test/fixture
failed=0

check() { # name, then a command whose exit status decides
    name=$1; shift
    if "$@"; then echo "ok   $name"; else echo "FAIL $name"; failed=$((failed + 1)); fi
}
same() { # expected-file, command...: the command's stdout must equal the file
    expected=$1; shift
    "$@" 2>/dev/null | diff -u "$expected" -
}
valid_json() { "$@" 2>/dev/null | ruby -rjson -e 'JSON.parse(STDIN.read)'; }
exits() { # code, command...
    code=$1; shift
    "$@" >/dev/null 2>&1
    test $? -eq "$code"
}
all_presets() { $BIN scan --preset rails --preset sidekiq --preset graphql --preset phlex --preset pundit "$@"; }

check "legacy tsv matches the spec's expected.tsv" same $FX/expected.tsv $BIN --roots rails.roots --tsv $FX
check "scan tsv"                       same $FX/expected-scan.tsv $BIN scan --roots rails.roots --format tsv $FX
check "presets give rails.roots' result" same $FX/expected-scan.tsv all_presets --format tsv $FX
check "batches tsv"                    same $FX/expected-batches.tsv $BIN batches --roots rails.roots --format tsv $FX
check "explain text"                   same $FX/expected-explain.txt $BIN explain 'Order#price_of' --roots rails.roots --color never $FX
check "scan json is valid"             valid_json $BIN scan --roots rails.roots --format json $FX
check "scan sarif is valid"            valid_json $BIN scan --roots rails.roots --format sarif $FX
check "--fail-on any exits 1"          exits 1 $BIN scan --roots rails.roots --fail-on any -q $FX
check "unknown option exits 2"         exits 2 $BIN scan --no-such-option $FX

# Regression cases: one small tree per bug or feature, with its expected scan.
C=test/cases
check "class Admin::Thing < ::Thing resolves to the top-level Thing" \
    same $C/self_superclass.expected.tsv $BIN scan --preset rails --format tsv $C/self_superclass
check "HAML and Slim views are read; their comments are not" \
    same $C/haml_slim.expected.tsv $BIN scan --format tsv $C/haml_slim
check "send(name) on self makes its class's dead methods uncertain, not dead" \
    same $C/uncertain_send.expected.tsv $BIN scan --format tsv $C/uncertain_send
check "explain shows the send that makes a definition uncertain" \
    same $C/uncertain_send.expected-explain.txt $BIN explain 'Config::DEFAULTS' --color never $C/uncertain_send
uncertain_ids() {
    $BIN scan --format json "$C/uncertain_send" 2>/dev/null | ruby -rjson -e '
      puts JSON.parse(STDIN.read)["uncertain"].map { |u| "#{u["id"]} #{u["send"]["call"]}" }' | tr '\n' ' '
}
check "scan json lists uncertain definitions with their send" test "$(uncertain_ids)" = \
    "method:Dispatcher#start send method:Dispatcher#stop send method:Dispatcher#prepare send module:Config send constant:Config::DEFAULTS send method:Factory.widget send method:Exporter#csv public_send "
check "< Superclass reaches constants defined inside the class" \
    same $C/kind_constant_owner.expected.tsv $BIN scan --no-config --roots $C/kind_constant_owner.roots --format tsv $C/kind_constant_owner

check "class OtherModule::ExampleClass looks constants up in itself, then the top level" \
    same $C/nesting_compact_path.expected.tsv $BIN scan --no-config --preset sidekiq --format tsv $C/nesting_compact_path
check "class ::OtherModule::ExampleClass in GenericModule includes GenericModule's mixin" \
    same $C/nesting_absolute_path.expected.tsv $BIN scan --no-config --preset sidekiq --format tsv $C/nesting_absolute_path
check "class ::ExampleClass in GenericModule prefers GenericModule's mixin to the top-level one" \
    same $C/nesting_absolute_class.expected.tsv $BIN scan --no-config --preset sidekiq --format tsv $C/nesting_absolute_class

if [ "$failed" -ne 0 ]; then echo "$failed check(s) failed"; exit 1; fi
echo "all checks passed"
