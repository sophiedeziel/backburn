PRISM_COMMIT := a1d1d96c368d49b752d06e361823a098b54f9a9d
# The source gem carries Prism's C sources, generated at PRISM_COMMIT, in
# ext/backburn/prism (`rake vendor:prism`). Without them, Prism is fetched with git.
VENDORED_PRISM := ext/backburn/prism
PRISM := $(if $(wildcard $(VENDORED_PRISM)/include/prism.h),$(VENDORED_PRISM),vendor/prism)
BUILD := build
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation
CPPFLAGS += -D_GNU_SOURCE -D_DARWIN_C_SOURCE -Isrc -I$(BUILD) -I$(PRISM)/include -MMD -MP -pthread

SRCS := $(shell find src -name '*.c' | sort)
OBJS := $(SRCS:src/%.c=$(BUILD)/%.o)

backburn: $(OBJS) $(PRISM)/build/libprism.a
	$(CC) $(CFLAGS) $(LDFLAGS) -pthread -o $@ $(OBJS) $(PRISM)/build/libprism.a -lm

$(BUILD)/%.o: src/%.c $(BUILD)/presets.h $(BUILD)/version.h | $(PRISM)/build/libprism.a
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# Presets are the roots files under presets/, compiled in so one binary works anywhere.
$(BUILD)/presets.h: scripts/embed-presets.sh $(wildcard presets/*.roots)
	@mkdir -p $(BUILD)
	sh scripts/embed-presets.sh presets/*.roots > $@

# lib/backburn/version.rb is the only version source, shared with the gemspec.
$(BUILD)/version.h: scripts/version-h.sh lib/backburn/version.rb
	@mkdir -p $(BUILD)
	sh scripts/version-h.sh lib/backburn/version.rb > $@.tmp && mv $@.tmp $@

# Prism's C library, pinned: its C API changed in 2026 (arena allocation, visitor callbacks).
# Prism's Makefile asks Ruby for the shared-library suffix, which the static
# library never uses; setting it keeps the build free of Ruby.
$(PRISM)/build/libprism.a: | $(PRISM)/include/prism/ast.h
	cd $(PRISM) && $(MAKE) build/libprism.a SOEXT=so

# Fetches Prism at the pinned commit and generates the C sources its templates
# produce (include/prism/ast.h is one), which needs Ruby.
vendor/prism/include/prism/ast.h:
	test -d vendor/prism || (git init -q vendor/prism && git -C vendor/prism fetch -q --depth 1 https://github.com/ruby/prism $(PRISM_COMMIT) && git -C vendor/prism checkout -q FETCH_HEAD)
	cd vendor/prism && ruby templates/template.rb

-include $(OBJS:.o=.d)

# glob_match must agree with fnmatch on every pattern (test/glob_test.c).
$(BUILD)/glob_test: test/glob_test.c src/core/glob.c src/core/glob.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -D_GNU_SOURCE -D_DARWIN_C_SOURCE -Isrc -o $@ test/glob_test.c src/core/glob.c

test: backburn $(BUILD)/glob_test
	$(BUILD)/glob_test
	sh test/run.sh

# Times scans of pinned public codebases; BASE=<binary> also times it and compares reports.
bench: backburn
	ruby scripts/bench.rb ./backburn $(BASE)

clean:
	rm -rf $(BUILD) backburn backburn.dSYM

.PHONY: test bench clean
