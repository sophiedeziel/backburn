# frozen_string_literal: true

require_relative "lib/backburn/version"

PRISM_COMMIT = File.read(File.expand_path("Makefile", __dir__))[/^PRISM_COMMIT := (\h+)$/, 1]
VENDORED_PRISM = "ext/backburn/prism"

desc "Copy Prism's C sources, generated at PRISM_COMMIT, into #{VENDORED_PRISM}"
task "vendor:prism" do
  # Fetches Prism into vendor/prism if needed and runs its templates.
  sh "make", "vendor/prism/include/prism/ast.h"
  head = `git -C vendor/prism rev-parse HEAD`.strip
  unless head == PRISM_COMMIT
    abort "vendor/prism is at #{head}, not PRISM_COMMIT #{PRISM_COMMIT}. Remove vendor/prism and run again."
  end
  rm_rf VENDORED_PRISM
  mkdir_p VENDORED_PRISM
  cp_r %w[vendor/prism/include vendor/prism/src], VENDORED_PRISM
  cp %w[vendor/prism/Makefile vendor/prism/LICENSE.md], VENDORED_PRISM
  File.write("#{VENDORED_PRISM}/COMMIT", "#{PRISM_COMMIT}\n")
end

desc "Build the source gem, which compiles backburn on install, into pkg/"
task build: "vendor:prism" do
  mkdir_p "pkg"
  sh "gem", "build", "backburn.gemspec", "--output", "pkg/backburn-#{Backburn::VERSION}.gem"
end

task default: :build
