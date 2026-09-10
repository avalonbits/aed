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
