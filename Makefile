PRISM := vendor/prism
PRISM_COMMIT := a1d1d96c368d49b752d06e361823a098b54f9a9d
BUILD := build
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation
CPPFLAGS += -D_GNU_SOURCE -D_DARWIN_C_SOURCE -Isrc -I$(BUILD) -I$(PRISM)/include -MMD -MP

SRCS := $(shell find src -name '*.c' | sort)
OBJS := $(SRCS:src/%.c=$(BUILD)/%.o)

backburn: $(OBJS) $(PRISM)/build/libprism.a
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(PRISM)/build/libprism.a -lm

$(BUILD)/%.o: src/%.c $(BUILD)/presets.h | $(PRISM)/build/libprism.a
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# Presets are the roots files under presets/, compiled in so one binary works anywhere.
$(BUILD)/presets.h: scripts/embed-presets.sh $(wildcard presets/*.roots)
	@mkdir -p $(BUILD)
	sh scripts/embed-presets.sh presets/*.roots > $@

# Prism's C library, pinned: its C API changed in 2026 (arena allocation, visitor callbacks).
$(PRISM)/build/libprism.a:
	test -d $(PRISM) || (git init -q $(PRISM) && git -C $(PRISM) fetch -q --depth 1 https://github.com/ruby/prism $(PRISM_COMMIT) && git -C $(PRISM) checkout -q FETCH_HEAD)
	cd $(PRISM) && ruby templates/template.rb && $(MAKE) build/libprism.a

-include $(OBJS:.o=.d)

test: backburn
	sh test/run.sh

clean:
	rm -rf $(BUILD) backburn backburn.dSYM

.PHONY: test clean
