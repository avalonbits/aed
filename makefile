# ----------------------------
# Makefile Options
# ----------------------------

NAME = aed
ICON = icon.png
DESCRIPTION = "Agoe text editor."
COMPRESSED = NO
ARCHIVED = NO
INIT_LOC = 0B0000

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# ----------------------------

include $(shell cedev-config --makefile)
