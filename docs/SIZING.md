# Why the numbers are the numbers

AED has four constants that decide how a large document behaves, and each one
was picked against a measurement rather than a guess:

| | where | value |
|---|---|---|
| how much RAM a document gets | [`main.c`](../src/main.c#L32) | 256 |
| how much of the buffer stays empty | [`prime_spare`](../src/text_buffer.c#L974) | a quarter |
| how far a slide moves | [`TB_CHUNK`](../src/text_buffer.h#L51) | 2 KiB |
| how close the cursor may get to an end | [`TB_MARGIN`](../src/text_buffer.h#L52) | 16 KiB |

[`DESIGN.md`](DESIGN.md) says what the paging machinery *is*. This document says
why it is sized the way it is, what was measured to find out, and what a second
open document would cost. Read section 4 of that first.

## Contents

* [1. What 256 buys](#1-what-256-buys)
* [2. The heap](#2-the-heap)
* [3. The sweep](#3-the-sweep)
* [4. Why a quarter](#4-why-a-quarter)
* [5. Why not a smaller buffer](#5-why-not-a-smaller-buffer)
* [6. Why two scratch files rather than one](#6-why-two-scratch-files-rather-than-one)
* [7. What a second document would cost](#7-what-a-second-document-would-cost)
* [8. How these were measured](#8-how-these-were-measured)

---

## 1. What 256 buys

`main` passes 256 to `ed_init`, which reaches
[`tb_init`](../src/text_buffer.c#L40), and that number is split two ways:

```c
int line_count = mem_kb << 5;                   // 8,192 line slots
int char_count = (mem_kb << 10) - line_count;   // 253,952 bytes
```

The line index is a separate allocation of `8192 * sizeof(int)`, and an `int` is
three bytes here. So a document costs **279,364 bytes**, where the 256 suggests
262,144:

| | bytes |
|---|---:|
| `char_buffer` | 253,952 |
| `line_buffer` | 24,576 |
| filename, scratch paths | 836 |
| **one open document** | **279,364** |

## 2. The heap

From `bin/aed.map`:

```
RAM_START  0x40000   RAM_SIZE 0x70000        448 KiB for the program
___heapbot 0x5c6a3   ___heaptop 0xb0000      342,365 bytes of heap
__stack = ___heaptop
```

The stack grows down from the same address the heap grows up to, so the figure
below is an upper bound on what is really spare.

| | bytes |
|---|---:|
| one open document | 279,364 |
| undo ring and records | 20,480 |
| clipboard | 8,192 |
| the prompt line | 256 |
| **in use** | **308,292** |
| **left, minus whatever the stack is using** | **34,073** |

**One document takes 90% of the heap.** That is the fact section 7 turns on.

## 3. The sweep

`slow.asm`, 419 KB over 7,510 lines, on MOS 3.0.2. Times are centiseconds. "3000
downs" is the arrow key held down through 3,000 lines, which is the path a user
feels; "seek" is a jump to the last line and back to the first.

| `mem_kb` | reserve | in memory | gap at mid | open | 3000 downs | 3000 ups | seek | save |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 256 | 1/2 | 126,917 | 64,411 | 158 | 86 | 56 | 206 | 26 |
| 256 | 1/3 | 169,299 | 42,874 | 222 | 52 | 42 | 216 | 22 |
| **256** | **1/4** | **190,451** | **32,100** | 258 | **36** | **34** | 220 | 20 |
| 256 | 1/8 | 222,208 | 16,034 | 326 | — | — | 238 | 16 |
| 128 | 1/4 | 95,182 | 17,024 | 122 | 150 | 68 | 208 | 28 |
| 128 | 1/8 | 111,080 | 8,834 | 140 | 144 | 70 | 224 | 28 |
| 128 | 1/16 | 119,007 | 4,975 | 150 | — | — | 274 | 24 |
| 64 | 1/4 | 47,602 | 9,451 | 86 | 120 | 66 | **wrong** | 30 |

Every row saved the document back byte for byte, including the last one: saving
streams the document and never asks where the cursor is.

Three effects pull in three directions.

**Open** tracks how much text goes into the window, at about 1.2 ms per KiB of
line indexing. Reserving more makes opening faster, because more of the file
goes straight to the tail and is never indexed twice.

**Arrow scrolling** tracks the window size. More text in memory means fewer
crossings of `TB_MARGIN`, and each crossing is a slide. This is the sharpest
axis in the table: 36 cs against 150 for the same 3,000 lines.

**Seeking across the document** barely moves — 206 to 238 across the whole
range. This is the axis everyone expects to dominate, and the amortisation
described in `DESIGN.md` section 3 is why it does not.

The 64 KiB row is worse than slow. After the arrow walk, a seek from line 7509
back to line 1 left the cursor on 7509. Its window holds 47,602 bytes against
`2 * TB_MARGIN` of 32,768, so [`tb_settle`](../src/text_buffer.c#L915) has
almost no room to work in and gives up. The same row seeked correctly in a run
without the arrow walk first, which makes it a state-dependent failure.

## 4. Why a quarter

The reserve is not tuned for speed. If it were, it would be smaller: 1/8 at 256
puts more text in memory, so it scrolls better than 1/4 and only opens slower.

It is a **safety floor**, and the thing it protects is the gap.

[`cb_rebalance`](../src/char_buffer.c#L102) splits free space three ways, so
after one the gap holds `free / 3`. A repaint walks a screenful through a
walker — [`refresh_screen`](../src/cmd_ops.c#L67) — and a walker moves the gap
as it reads. The copy's writes stay behind the original's `cend_` only while the
gap is wider than the distance the copy has travelled. Narrower, and the walker
overwrites the document it is painting.

`TB_MARGIN` already names the worst case: 128 x 96 is the widest mode the VDP
offers, so a dense screenful is 12,288 bytes.

The gap column above is what the sweep observed with the cursor in the middle of
the document, which is wider than the floor: a give at one end leaves half of
what it did not need at the cursor. The floor is what a plain rebalance leaves,
and that is the number the reserve has to be chosen against.

| `char_buffer` | reserve | free pool | gap after a rebalance | against 12 KiB |
|---:|---:|---:|---:|---|
| 253,952 | 1/4 | 63,488 | 21,163 | 1.72x |
| 253,952 | 1/6 | 42,325 | 14,108 | 1.15x |
| 253,952 | 1/7 | 36,279 | 12,093 | on the line |
| 253,952 | 1/8 | 31,744 | 10,581 | short |
| 126,976 | 1/4 | 31,744 | 10,581 | short |
| 126,976 | 1/3 | 42,325 | 14,108 | 1.15x |

**A quarter of 248 KiB is the tightest ratio that leaves real headroom over a
worst-case repaint.** Anything past 1/7 is under it, and a document painted on a
128-column screen would be corrupted rather than merely slow.

The sweep's 1/8 and 1/16 rows pass because `slow.asm` has 52-byte lines: the
walker moved 3,773 bytes for 60 rows, well inside even a 2,645-byte gap most of
the time. They would fail on a wide screen full of long lines, which is exactly
the failure this floor exists to rule out.

## 5. Why not a smaller buffer

Halving `mem_kb` to 128 looks attractive in the table — open drops from 258 to
122 — and it is the wrong trade twice over.

**The reserve would have to grow.** At 126,976 bytes even a quarter
leaves a 10,581-byte gap, already under the 12 KiB floor. Keeping the floor at
128 KiB means reserving a third, which leaves an 84 KiB window for `2 *
TB_MARGIN` of 32 KiB to sit in.

**Arrow scrolling costs four times as much.** 150 cs against 36 for 3,000 lines,
and that is on the emulator, where a slide's disk traffic is nearly free. On a
real card the gap is wider. Opening a file happens once; scrolling happens all
day.

## 6. Why two scratch files rather than one

One file holding both sides, with `HEAD` growing up from zero and `TAIL`
consumed from an offset, is the same shape as the gap buffer it is backing:

```
[ HEAD text ][ gap ][ TAIL text ]
0        head_len_  tail_start_  tail_end_
```

It saves a MOS handle out of the seven available, a file on the card, one
`STORE_PATH_MAX` buffer, and two `mos_fopen` calls at startup — about 22 ms of a
2.58 second open.

It saves nothing else, because the layout is unchanged in every respect that
costs anything:

* A slide down moves `head_len_` and `tail_start_` right by the same amount, so
  the gap between them is invariant under scrolling. It shrinks only when text
  is inserted, which is what `STORE_HEADROOM` is already for.
* At open `head_len_` is zero and `TAIL` still has to start at a non-zero
  offset, so the bytes below it still have to exist before `TAIL` is appended
  after them. That is the headroom write, unchanged, and for the reason
  [`STORE_HEADROOM`](../src/doc_store.h#L66) records: FatFS clips a seek past
  the end of a file rather than extending it.
* All four push and pop operations already read as "seek to an offset on a
  held handle and transfer". They would be identical.

Measured: 100 slides of a 2 KiB write and a 2 KiB read 240 KiB apart cost 12 cs
on two handles and 12 cs on one, on MOS 3.0.2; 14 and 14 on Console8. That
covers the eZ80 side only — see section 8 on what the emulator does not model.

Against those small wins, the two sides would share one address space, so a
mistake in one could reach the other; today a head write cannot touch the tail's
file at all. [`store_tail_has_room`](../src/doc_store.h#L138) would have to
account for both, so a push back could fail because the *head* grew.

## 7. What a second document would cost

Handles are not the constraint. MOS gives out seven, a document uses two and the
clipboard one, so three documents would fit.

Heap is the constraint, by a factor of two:

| per document | two documents, two undo logs, clipboard, prompt | heap left |
|---:|---:|---:|
| 256 KiB — 279,364 | 587,656 | **impossible** |
| 128 KiB — 140,100 | 329,608 | 12,757, minus the stack |
| 96 KiB — 105,284 | 259,976 | 82,389 |

So a second document means a smaller buffer, and section 5 says a smaller buffer
means a bigger reserve, and a bigger reserve means a smaller window. The chain
ends at 96 KiB per document with barely 57 KiB of window for two 16 KiB margins
to sit inside.

**The 12 KiB floor is what holds the chain up, and it is an assumption.** It
comes from the widest mode the VDP can be put in. AED knows its real geometry at
runtime, and at 80 x 60 a dense screenful is 4,800 bytes.

Reading the floor from `rows_ * cols_` would let the reserve fall far enough to
make 128 KiB per document work. It would also make how many documents can be
open depend on the video mode, which is a strange thing for an editor to do: a
user at 1024 x 768 would be told to lower their resolution before they could
open a second file. That rules the idea out on its own.

What is left is to remove the floor instead of sizing it.

The floor exists because a walker moves the gap, and a read-only walker has no
need of a gap at all. A line's bytes can be found from a byte offset: the prefix
runs from `lo_` to `curr_`, the suffix from `cend_` to `hi_`, and at most one
line straddles the boundary between them.
[`split_line`](../src/text_buffer.h#L175) already hands a line back in two
pieces, which is exactly what that one line needs.
[`tb_copy`](../src/text_buffer.c#L1787) already marks a copy as read-only, and
every mutator already refuses on that flag, so the walker is a distinct enough
thing to give a different implementation to.

A walker that carried an offset instead of moving the buffer would:

* make painting pointer arithmetic rather than a `memmove` per line, which is
  most of what a repaint costs today
* remove the reserve's lower bound, letting `prime_spare` be chosen on the
  amortisation curve in section 3 alone
* let the buffer be sized from the heap budget, at any video mode

That is the change a second document waits on. None of the constants above need
to move until it lands.

## 8. How these were measured

On `fab-agon-emulator` 1.2.4, headless, both firmwares, with a build of the
editor's own sources driven by a harness in place of `main` — so every number is
AED's own code rather than a microbenchmark of something near it.

**The emulator does not model the card.** With `--sdcard <directory>` the host
filesystem serves MOS's file calls, and `test/probes/README.md` records that
reads and seeks measure 0 cs there. Every figure in this document therefore
under-charges disk.

That bias has a direction, and it runs the helpful way: a smaller buffer writes
more to the tail and slides more often, so the rows this document argues against
would look **worse** on hardware than they look here. The relative comparisons
hold; the absolute numbers are optimistic.

The one measurement this invalidates outright is the handle probe in section 6.
FatFS barely runs under `--sdcard`, so whether one file handle seeking between
two distant offsets thrashes its sector window the way two handles would not is
**unanswered**. It needs hardware, or a raw image through `--sdcard-img`.
