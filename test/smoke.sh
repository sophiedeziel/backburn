#!/bin/sh
# Installs a backburn gem and checks the installed command, as a user would
# run it. CI runs this for every gem in a clean container or runner:
#   sh test/smoke.sh pkg/backburn-0.1.0-x86_64-linux.gem
set -eu
cd "$(dirname "$0")/.."
gem="$1"
version=$(sed -n 's/^ *VERSION *= *"\([^"]*\)".*/\1/p' lib/backburn/version.rb)

gem install --no-document --local "$gem"
gem list backburn
bin=$(command -v backburn)
echo "backburn is $bin"

test "$(backburn --version)" = "backburn $version"
backburn scan --color never test/fixture
BACKBURN="$bin" sh test/run.sh
