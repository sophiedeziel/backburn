# frozen_string_literal: true

require "etc"
require "rubygems/package"
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

# This machine's gem platform, without the OS version on macOS, so the gem
# installs on every macOS release: arm64-darwin, x86_64-linux, aarch64-linux-musl.
def native_platform
  local = Gem::Platform.local
  Gem::Platform.new([local.cpu, local.os, local.os == "linux" ? local.version : nil]).to_s
end

desc "Build backburn, run make test, then package it as a precompiled gem for " \
     "this platform, or for each platform in PLATFORM, into pkg/"
task :native do
  platforms = (ENV["PLATFORM"] || native_platform).split
  if RUBY_PLATFORM.include?("darwin")
    # Build for macOS 11 and later, not only the macOS this runs on. Unlike
    # MACOSX_DEPLOYMENT_TARGET, the flag works with every clang wrapper.
    ENV["CC"] = "#{ENV.fetch("CC", "cc")} -mmacosx-version-min=11.0"
  elsif Gem::Platform.local.version == "musl"
    # A static musl binary runs on any Linux, glibc or musl.
    ENV["LDFLAGS"] ||= "-static -s"
  end
  # Start clean, so every object, Prism's included, is built with these settings.
  sh "make", "clean"
  rm_rf %W[vendor/prism/build #{VENDORED_PRISM}/build]
  sh "make", "-j#{Etc.nprocessors}", "test"
  rm_rf "libexec"
  mkdir_p "libexec"
  cp "backburn", "libexec/backburn"

  mkdir_p "pkg"
  platforms.each do |platform|
    spec = Gem::Specification.load("backburn.gemspec").dup
    spec.platform = Gem::Platform.new(platform)
    spec.extensions = []
    spec.files = spec.files.reject { |f| f.start_with?("ext/", "scripts/", "src/") || f == "Makefile" }
    spec.files += ["libexec/backburn"]
    mv Gem::Package.build(spec), "pkg/"
  end
end

task default: :build
