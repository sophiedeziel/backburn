#!/usr/bin/env ruby
# frozen_string_literal: true

# Times `backburn scan` on pinned public Rails codebases. Given a second binary,
# it times that one too, alternating runs, and checks both report the same thing.
#
#   ruby scripts/bench.rb ./backburn                     # make bench
#   ruby scripts/bench.rb ./backburn /tmp/backburn-main  # make bench BASE=/tmp/backburn-main
#
# RUNS timed runs (default 5) per binary and codebase, after one warm-up run
# that fills the file cache. The medians are reported. Codebases are downloaded
# once, as GitHub tarballs, into BENCH_DIR (default ~/.cache/backburn-bench).

require "fileutils"
require "shellwords"

CODEBASES = [
  ["rails/rails", "54c0745df4d1b91bddb5ccc27ae566e5bfd817b4"],
  ["discourse/discourse", "535eeae763e249e2f03dc9786e05a703a0c09c16"],
].freeze
SCAN = ["scan", "--no-config", "--preset", "rails"].freeze

bins = ARGV.reject(&:empty?)
abort("usage: ruby scripts/bench.rb BINARY [BASE_BINARY]") unless (1..2).cover?(bins.size)
bins.each { |b| abort("bench: #{b} is not an executable") unless File.executable?(b) }
runs = Integer(ENV.fetch("RUNS", "5"))
cache = ENV["BENCH_DIR"] || File.join(ENV["XDG_CACHE_HOME"] || File.join(Dir.home, ".cache"), "backburn-bench")

def fetch(cache, repo, sha)
  dir = File.join(cache, "#{repo.tr("/", "-")}-#{sha[0, 12]}")
  return dir if File.directory?(dir)

  part = "#{dir}.part"
  FileUtils.rm_rf(part)
  FileUtils.mkdir_p(part)
  url = "https://codeload.github.com/#{repo}/tar.gz/#{sha}"
  warn("bench: downloading #{repo} at #{sha[0, 12]}")
  system("curl -fsSL #{url.shellescape} | tar -xzf - -C #{part.shellescape} --strip-components=1") ||
    abort("bench: could not download #{url}")
  File.rename(part, dir)
  dir
end

def elapsed_ms(bin, dir)
  t = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  system(bin, *SCAN, "--format", "tsv", "-q", dir, out: File::NULL, err: File::NULL)
  ((Process.clock_gettime(Process::CLOCK_MONOTONIC) - t) * 1000).round
end

def median(times)
  times.sort[times.size / 2]
end

def report(bin, dir)
  IO.popen([bin, *SCAN, "--format", "tsv", "-q", dir], err: File::NULL, &:read)
end

head = ["codebase", "files", "definitions", *bins.map { |b| "#{b} (ms)" }]
head += ["change", "same report"] if bins.size == 2
puts("| #{head.join(" | ")} |", "|#{head.map { "---" }.join("|")}|")

CODEBASES.each do |repo, sha|
  dir = fetch(cache, repo, sha)
  summary = IO.popen([bins[0], *SCAN, "--color", "never", dir], err: File::NULL, &:gets).to_s
  files, defs = summary.match(/(\d+) files · (\d+) definitions/)&.captures || ["?", "?"]
  bins.each { |b| elapsed_ms(b, dir) } # warm-up
  times = bins.map { [] }
  runs.times { bins.each_with_index { |b, i| times[i] << elapsed_ms(b, dir) } }
  med = times.map { |t| median(t) }
  row = [repo, files, defs, *med]
  if bins.size == 2
    row << format("%+.0f%%", (med[0] - med[1]) * 100.0 / med[1])
    row << (report(bins[0], dir) == report(bins[1], dir) ? "yes" : "**no**")
  end
  puts("| #{row.join(" | ")} |")
end
