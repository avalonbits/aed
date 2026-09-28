# ----------------------------
# Makefile Options
# ----------------------------

NAME=aed

# Select option for Argument Processing at 'int main( int argc, char* argv[] )'
# 0: Simple Command Line Processing
# 1: Complex Command Line Processing - for Redirection & Quoting
LDHAS_ARG_PROCESSING = 0
LDHAS_EXIT_HANDLER = 0

# ----------------------------
#
include $(shell agondev-config --makefile)

# Header dependencies, which AgonDev's makefile does not track.
#
# Its rule is `obj/%.o: src/%.c` and nothing more, so editing a header rebuilds
# nothing that includes it. That is not a slow build, it is a wrong one: adding
# a field to the middle of a struct in screen.h left screen.o and editor.o
# rebuilt against the new layout while cmd_ops.o, user_input.o and main.o went
# on reading every field after it at the old offset. The binary was the right
# size, linked cleanly, passed the whole host suite -- which compiles every
# source together from scratch and so cannot see this -- and painted garbage.
#
# -MMD writes obj/<name>.d beside each object, listing the headers it used.
# -MP adds a phony target for each of those headers so that deleting one does
# not leave a dead prerequisite and a build that refuses to start.
#
# These come after the include on purpose: makefile.inc assigns CFLAGS with
# `=`, so anything set before it is discarded.
CFLAGS   += -MMD -MP
CXXFLAGS += -MMD -MP

# Pulled in for the objects built so far. A source whose .d does not exist yet
# has no object either, so it is compiled regardless -- the missing dependency
# information cannot cause a stale build, only the first one.
-include $(shell find $(OBJDIR) -name '*.d' 2>/dev/null)

# The two libraries AED is built from, and that ade links as well.
#
#   src/core  libedcore.a  the document: buffers, paging, undo, the clipboard,
#                          grammars, themes, the settings engine. No VDP and
#                          no keyboard -- nothing in it includes anything else.
#   src/ui    libedui.a    views, the screen, keys, prompts, the editing
#                          commands and the loop. Built on the core.
#   src       AED itself   main, its keys, help, banner and settings, on both.
#
# Every source is compiled with all three on the include path, and AgonDev's
# rules already compile src/**/*.c into the matching obj/ directory. What
# changes is the link: AED's own objects and the two archives, rather than
# every object there is, so the binary is built the way ade's will be and a
# symbol AED needs from the wrong layer shows up here as well as in the tests.
CFLAGS += -Isrc/core -Isrc/ui -Isrc

CORE_OBJS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(wildcard $(SRCDIR)/core/*.c)) \
             $(patsubst $(SRCDIR)/%.asm,$(OBJDIR)/%.o,$(wildcard $(SRCDIR)/core/*.asm))
UI_OBJS   := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(wildcard $(SRCDIR)/ui/*.c))
AED_OBJS  := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(wildcard $(SRCDIR)/*.c))

LIBCORE := $(BINDIR)/libedcore.a
LIBUI   := $(BINDIR)/libedui.a

# Rebuilt whole rather than updated: ar adds and replaces members but never
# drops one, so a source deleted from core would otherwise live on in the
# archive and go on satisfying links it should now fail.
$(LIBCORE): $(CORE_OBJS) | $(BINDIR)
	@echo [Creating library $@]
	$(V)rm -f $@ && $(AR) $(ARFLAGS) $@ $(CORE_OBJS)

$(LIBUI): $(UI_OBJS) | $(BINDIR)
	@echo [Creating library $@]
	$(V)rm -f $@ && $(AR) $(ARFLAGS) $@ $(UI_OBJS)

libs: $(LIBCORE) $(LIBUI)

# makefile.inc links $(OBJS) and then $(LINKERLIBFLAGS), both read when the
# link runs. So AED's objects, and the libraries in the order they depend on
# each other: the UI first, then the core it uses.
$(LINKBINARY): $(LIBUI) $(LIBCORE)
OBJS := $(AED_OBJS)
PROJECTLIBDIR := $(BINDIR)
LIBS := -ledui -ledcore

.PHONY: libs
