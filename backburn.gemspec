# frozen_string_literal: true

require_relative "lib/backburn/version"

Gem::Specification.new do |spec|
  spec.name = "backburn"
  spec.version = Backburn::VERSION
  spec.authors = ["Sophie Déziel"]
  spec.email = ["courrier@sophiedeziel.com"]

  spec.summary = "Finds Ruby code that no production path can reach."
  spec.description = <<~DESC.tr("\n", " ").strip
    Backburn parses a Ruby codebase with Prism, builds a name-resolved reference
    graph, marks everything reachable from your entry points and reports what
    stays unmarked, including dead chains and dead cycles, in batches you can
    delete one commit at a time.
  DESC
  spec.homepage = "https://github.com/sophiedeziel/backburn"
  spec.license = "MIT"
  # Only exe/backburn is Ruby, and it just execs the native binary.
  spec.required_ruby_version = ">= 2.7"

  spec.metadata = {
    "source_code_uri" => spec.homepage,
    "bug_tracker_uri" => "#{spec.homepage}/issues",
    "rubygems_mfa_required" => "true"
  }

  # Everything needed to build and document backburn, including Prism's C
  # sources vendored by `rake vendor:prism`. test/, samples/, vendor/ (Prism's
  # git checkout), the benchmark script and the deprecated gem_usage.rb stay out.
  spec.files = Dir.chdir(__dir__) do
    Dir[
      "{docs,exe,lib,presets,scripts,src}/**/*", "Makefile", "README.md", "LICENSE", "rails.roots",
      "ext/backburn/extconf.rb", "ext/backburn/prism/{COMMIT,LICENSE.md,Makefile}",
      "ext/backburn/prism/{include,src}/**/*"
    ].select { |f| File.file?(f) } - ["scripts/bench.rb"]
  end
  spec.bindir = "exe"
  spec.executables = ["backburn"]
  # Builds the binary into libexec/backburn on `gem install`.
  spec.extensions = ["ext/backburn/extconf.rb"]
end
