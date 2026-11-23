# frozen_string_literal: true

module Backburn
  # The only version source: backburn.gemspec reads it, and the Makefile turns
  # it into build/version.h for `backburn --version` and the report headers.
  VERSION = "0.1.0"
end
