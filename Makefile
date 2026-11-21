LIB_NAME = board_games
BIN_NAME = demo

SRCDIR     = src
INCDIR     = include
BUILDDIR   = build
BINDIR     = bin
CODEGENDIR = codegen

CC       = clang
AR       = ar
ARFLAGS  = rcs

CFLAGS   = -std=c99 -O3 -DNDEBUG -march=native -flto -funroll-loops \
           -Wall -Wextra -Wpedantic -Wconversion \
           -Wno-incompatible-pointer-types-discards-qualifiers \
           -ffunction-sections -fdata-sections \
           -MMD -MP \
           -I$(INCDIR) -I$(BUILDDIR)

UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
    LDFLAGS = -flto -Wl,-dead_strip
else
    LDFLAGS = -flto -Wl,--gc-sections
endif

# ── Codegen (convention: codegen/generate_foo.c → build/foo.h) ───────────────
CODEGEN_SRCS   = $(wildcard $(CODEGENDIR)/generate_*.c)
GENERATED_HDRS = $(patsubst $(CODEGENDIR)/generate_%.c,$(BUILDDIR)/%.h,$(CODEGEN_SRCS))

LIB_SRCS  = $(filter-out $(SRCDIR)/main.c, $(wildcard $(SRCDIR)/*.c))
LIB_OBJS  = $(LIB_SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
LIB_DEPS  = $(LIB_OBJS:.o=.d)

MAIN_SRC  = $(SRCDIR)/main.c
MAIN_OBJ  = $(BUILDDIR)/main.o
MAIN_DEP  = $(BUILDDIR)/main.d

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

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# ── Codegen rules (convention: codegen/generate_foo.c → build/foo.h) ─────────
# Build code-generators
$(BUILDDIR)/generate_%: $(CODEGENDIR)/generate_%.c | $(BUILDDIR)
	$(CC) -std=c99 -O2 -I$(INCDIR) -o $@ $<

# Run code-generators to produce their headers
$(BUILDDIR)/%.h: $(BUILDDIR)/generate_%
	$< > $@

# ── Per-file codegen dependencies (one line each) ────────────────────────────
$(BUILDDIR)/tic_tac_toe.o: $(BUILDDIR)/ttt_has_win_bit_array.h

$(BUILDDIR) $(BINDIR):
	mkdir -p $@

-include $(LIB_DEPS) $(MAIN_DEP)

clean:
	rm -rf $(BUILDDIR) $(BINDIR)

help:
	@echo "Targets:"
	@echo "  all   (default)  Build the static library"
	@echo "  lib              Build $(STATIC_LIB)"
	@echo "  demo             Build $(EXECUTABLE) (includes lib)"
	@echo "  clean            Remove $(BUILDDIR)/ and $(BINDIR)/"
	@echo "  help             Show this message"
