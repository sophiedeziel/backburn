# frozen_string_literal: true

# `gem install backburn` runs this, then `make clean`, `make` and `make install`
# in this directory. The Makefile it writes builds the backburn binary with the
# gem's top-level Makefile, from the Prism sources vendored in ext/backburn/prism,
# and installs it as libexec/backburn, where exe/backburn runs it. A C compiler
# and make are the only requirements: no git, no network.
require "shellwords"

root = File.expand_path("../..", __dir__)
abort "extconf.rb: #{root}/Makefile not found" unless File.exist?(File.join(root, "Makefile"))
# Quoted for the shell, with `$` doubled for make.
dir = Shellwords.escape(root).gsub("$", "$$")

File.write("Makefile", <<~MAKE)
  ROOT = #{dir}

  all:
  \t$(MAKE) -C $(ROOT) backburn

  install: all
  \tmkdir -p $(ROOT)/libexec
  \tmv -f $(ROOT)/backburn $(ROOT)/libexec/backburn
  \t$(MAKE) -C $(ROOT) clean
  \trm -rf $(ROOT)/ext/backburn/prism/build

  clean:
  \t$(MAKE) -C $(ROOT) clean

  .PHONY: all install clean
MAKE
