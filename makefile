# ----------------------------
# Makefile Options
# ----------------------------

NAME = aed
ICON = icon.png
DESCRIPTION = "Agoe text editor."
COMPRESSED = NO
ARCHIVED = NO
INIT_LOC = 0B0000
BSSHEAP_LOW = 040000
BSSHEAP_HIGH = 0A7FFF
STACK_HIGH = 0AFFFF

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# ----------------------------

include $(shell cedev-config --makefile)
