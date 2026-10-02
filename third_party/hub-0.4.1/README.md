# hub 0.4.1 client library

hub's client API -- `include/hub/hub.h` and the agondev build of `libhub.a` --
copied here so AED builds without a hub checkout beside it. AED uses it for
CTRL+R, which hands a build and a run to hub and comes back afterwards.

hub is at https://github.com/avalonbits/hub. Its agondev package,
`hub-agondev-<version>.zip` on each release, holds the same two files.

From hub 0.4.1 (commit abd7680, "give a user program the prompt's font, and put
it back after"): `include/hub/hub.h` and `build/lib/agondev/libhub.a`, the
latter assembled by zap from `lib/hub_glue.s`. Replace both together when
moving to a newer hub, and rename the directory to match. hub 0.4.2, the first
public release, has the same library objects; its header differs only in the
comment on hub_block.
