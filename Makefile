# Options: release, debug (default)
BUILD ?= debug

LIB_NAME = board_games
BIN_NAME = demo

SRCDIR     = src
INCDIR     = include
BUILDDIR   = build/$(BUILD)
BINDIR     = bin/$(BUILD)
CODEGENDIR = codegen

CC       = clang
AR       = llvm-ar
ARFLAGS  = rcs

CFLAGS_COMMON = -std=c99 -march=native \
                -Wall -Wextra -Wpedantic -Wconversion \
                -Wno-incompatible-pointer-types-discards-qualifiers \
                -Warray-bounds-pointer-arithmetic \
                -ffunction-sections -fdata-sections \
                -MMD -MP \
                -I$(INCDIR) -I$(BUILDDIR)

ifeq ($(BUILD),release)
    CFLAGS = $(CFLAGS_COMMON) -O3 -DNDEBUG -flto -funroll-loops -g
    LDFLAGS_MODE = -flto
else ifeq ($(BUILD),debug)
    CFLAGS = $(CFLAGS_COMMON) -O0 -g3 \
	     -fsanitize=address,undefined \
	     -fno-omit-frame-pointer
    LDFLAGS_MODE = -fsanitize=address,undefined
else
    $(error Unknown BUILD mode '$(BUILD)'. Use 'debug' or 'release')
endif

UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
    LDFLAGS = $(LDFLAGS_MODE) -Wl,-dead_strip
else
    LDFLAGS = $(LDFLAGS_MODE) -Wl,--gc-sections
endif

# ── Codegen (convention: codegen/generate_foo.c → build/foo.h) ───────────────
CODEGEN_SRCS   = $(wildcard $(CODEGENDIR)/generate_*.c)
GENERATED_HDRS = $(patsubst $(CODEGENDIR)/generate_%.c,$(BUILDDIR)/%.h,$(CODEGEN_SRCS))
CODEGEN_OBJDIR = $(BUILDDIR)/codegen
CODEGEN_OBJS   = $(patsubst $(CODEGENDIR)/%.c,$(CODEGEN_OBJDIR)/%.o,$(CODEGEN_SRCS))
CODEGEN_DEPS   = $(CODEGEN_OBJS:.o=.d)
CODEGEN_CFLAGS = -std=c99 -O2 -I$(INCDIR) -I$(BUILDDIR)

# Library sources are the shared top-level sources plus every per-module
# subdirectory of src/ (add new modules here); src/main.c is the demo entry
# point and stays out of the library
SRC_MODULES = games agents

LIB_SRCS  = $(filter-out $(SRCDIR)/main.c, $(wildcard $(SRCDIR)/*.c)) \
            $(foreach module,$(SRC_MODULES),$(wildcard $(SRCDIR)/$(module)/*.c))
LIB_OBJS  = $(LIB_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
LIB_DEPS  = $(LIB_OBJS:.o=.d)

MAIN_SRC  = $(SRCDIR)/main.c
MAIN_OBJ  = $(BUILDDIR)/main.o
MAIN_DEP  = $(BUILDDIR)/main.d

# Object files mirror the src/ layout, so build/ needs the same subdirectories
OBJ_DIRS  = $(sort $(BUILDDIR) $(CODEGEN_OBJDIR) \
            $(patsubst %/,%,$(dir $(LIB_OBJS) $(MAIN_OBJ))))

STATIC_LIB = $(BINDIR)/$(LIB_NAME)_lib.a
EXECUTABLE = $(BINDIR)/$(BIN_NAME)

.PHONY: all lib demo clean help

# Default: build the library only
all: lib

lib: $(STATIC_LIB)

# Demo executable - used for demoing and smoke testing
demo: $(EXECUTABLE)

$(STATIC_LIB): $(LIB_OBJS) | $(BINDIR)
	$(AR) $(ARFLAGS) $@ $^

# Demo executable - compile main.c and link against the static lib
$(EXECUTABLE): $(MAIN_OBJ) $(STATIC_LIB) | $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(MAIN_OBJ) $(STATIC_LIB)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(OBJ_DIRS)
	$(CC) $(CFLAGS) -c $< -o $@

# ── Codegen rules (convention: codegen/generate_foo.c → build/foo.h) ─────────
# A generator that fails part-way (e.g. its output contradicts a constant in
# the header it generates against) must not leave a half-written header behind
# for the next build to compile against
.DELETE_ON_ERROR:

# Compile a generator, tracking the headers it reads: a generator that derives
# its output from a constant in a header must be rebuilt when that header
# changes, or it goes on emitting (or refusing to emit) against the old value
$(CODEGEN_OBJDIR)/%.o: $(CODEGENDIR)/%.c | $(OBJ_DIRS)
	$(CC) $(CODEGEN_CFLAGS) -MMD -MP -c $< -o $@

# Link a generator: its own object, plus any library sources it pulls in
# (below). Those are compiled here rather than reused from $(BUILDDIR) so that
# a generator never inherits the build mode's sanitizers or LTO. Generated
# headers listed as prerequisites are dependencies to track, not inputs to
# compile, hence the filter
$(BUILDDIR)/generate_%: $(CODEGEN_OBJDIR)/generate_%.o | $(BUILDDIR)
	$(CC) $(CODEGEN_CFLAGS) -o $@ $(filter %.o %.c,$^)

# Run code-generators to produce their headers
$(BUILDDIR)/%.h: $(BUILDDIR)/generate_%
	$< > $@

# Prerequisites for code-generators
$(BUILDDIR)/generate_ttt_zobrist_hashes: $(SRCDIR)/prng.c
$(BUILDDIR)/generate_ttt_board_to_index: $(SRCDIR)/games/tic_tac_toe.c
$(BUILDDIR)/generate_ttt_board_to_index: $(BUILDDIR)/ttt_has_win_bit_array.h

# ── Per-file codegen dependencies (one line each) ────────────────────────────
$(BUILDDIR)/games/tic_tac_toe.o: $(BUILDDIR)/ttt_has_win_bit_array.h
$(BUILDDIR)/games/tic_tac_toe.o: $(BUILDDIR)/ttt_zobrist_hashes.h
$(BUILDDIR)/agents/board_index.o: $(BUILDDIR)/ttt_board_to_index.h

$(OBJ_DIRS) $(BINDIR):
	mkdir -p $@

-include $(LIB_DEPS) $(MAIN_DEP) $(CODEGEN_DEPS)

clean:
	rm -rf $(BUILDDIR) $(BINDIR)

help:
	@echo "Targets:"
	@echo "  all   (default)  Build the static library"
	@echo "  lib              Build $(STATIC_LIB)"
	@echo "  demo             Build $(EXECUTABLE) (includes lib)"
	@echo "  clean            Remove $(BUILDDIR)/ and $(BINDIR)/"
	@echo "  help             Show this message"
	@echo ""
	@echo "Build modes (BUILD=<mode>):"
	@echo "  debug   (default)  -O0, -g3, ASan + UBSan + LSan, assertions enabled"
	@echo "                     (MacOS: run with ASAN_OPTIONS=detect_leaks=1)"
	@echo "  release            -O3, -DNDEBUG, LTO, assertions disabled"
