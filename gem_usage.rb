#!/usr/bin/env ruby
# frozen_string_literal: true

# Deprecated: `backburn gems DIR` replaces this script and will be the only
# spelling after the next release. This shim keeps `ruby gem_usage.rb DIR`
# working by running the backburn binary that sits next to it.
backburn = File.expand_path("backburn", __dir__)
abort "gem_usage.rb: #{backburn} not found; run `make` first" unless File.executable?(backburn)

warn "gem_usage.rb is deprecated and will be removed in the next release; use `backburn gems DIR`"
exec backburn, "gems", *ARGV
