# Why the numbers are the numbers

AED has four constants that decide how a large document behaves, and each one
was picked against a measurement rather than a guess:

| | where | value |
|---|---|---|
| how much RAM a document gets | [`main.c`](../src/main.c#L32) | 256 |
| how much of the buffer stays empty | [`prime_spare`](../src/text_buffer.c#L1215) | a quarter |
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

| `mem_kb` | reserve | in memory | open | 3000 downs | 3000 ups | seek | save |
|---:|---:|---:|---:|---:|---:|---:|---:|
| **256** | **1/4** | **190,451** | **94** | 38 | 36 | **232** | 22 |
| 256 | 1/8 | 222,208 | 104 | 34 | 30 | 250 | 20 |
| 256 | 1/16 | 238,037 | 110 | **32** | **30** | 284 | **16** |
| 128 | 1/8 | 111,080 | **86** | 70 | 70 | 234 | 26 |
| 128 | 1/16 | 119,007 | 88 | 82 | 82 | 256 | 26 |

Taken before a walker stopped moving the gap, which is why the reserve could
not go below about a seventh — see section 4:

| `mem_kb` | reserve | in memory | gap at mid | open | 3000 downs | seek |
|---:|---:|---:|---:|---:|---:|---:|
| 256 | 1/2 | 126,917 | 64,411 | 158 | 86 | 206 |
| 256 | 1/3 | 169,299 | 42,874 | 222 | 52 | 216 |
| 256 | 1/4 | 190,451 | 32,100 | 258 | 36 | 220 |
| 256 | 1/8 | 222,208 | 16,034 | 326 | — | 238 |
| 128 | 1/4 | 95,182 | 17,024 | 122 | 150 | 208 |
| 128 | 1/8 | 111,080 | 8,834 | 140 | 144 | 224 |
| 64 | 1/4 | 47,602 | 9,451 | 86 | 120 | **wrong** |

Every row saved the document back byte for byte, including the last one: saving
streams the document and never asks where the cursor is.

Opening a 419 KB file went from 2.58 seconds to 0.94 between those two tables,
and none of it was the loader. `cb_room_back` used to leave exactly the `n`
bytes it had been asked for above `hi_`, giving half the leftover to the gap
instead -- so the next chunk of the load needed another full move of the live
text, and every chunk after that did too. The gap gets an eighth now and the
rest goes to the ends, which is where a slide spends it.

Three effects pull in three directions.

**Open** tracks how much text goes into the window, at about 0.4 ms per KiB of
line indexing. Reserving more makes opening faster, because more of the file
goes straight to the tail and is never indexed twice.

**Arrow scrolling** tracks the window size. More text in memory means fewer
crossings of `TB_MARGIN`, and each crossing is a slide. This is the axis that
decides the buffer's size: 38 cs at 256 KiB against 70 at 128 for the same
3,000 lines.

**Seeking across the document** moves least of the three — 232 to 284 across
the range. This is the axis everyone expects to dominate, and the amortisation
described in `DESIGN.md` section 3 is why it does not.

The 64 KiB row is worse than slow. After the arrow walk, a seek from line 7509
back to line 1 left the cursor on 7509. Its window holds 47,602 bytes against
`2 * TB_MARGIN` of 32,768, so [`tb_settle`](../src/text_buffer.c#L1156) has
almost no room to work in and gives up. The same row seeked correctly in a run
without the arrow walk first, which makes it a state-dependent failure.

## 4. Why a quarter

It used to be a floor rather than a choice, and it is worth saying what the
floor was before saying what replaced it.

[`cb_rebalance`](../src/char_buffer.c#L116) used to split free space three
ways, so the gap held `free / 3`. A repaint walks a screenful through a walker
— [`refresh_screen`](../src/cmd_ops.c#L66) — and a walker read by *moving* the
gap. The copy's writes stay behind the original's `cend_` only while the gap is
wider than the distance the copy has travelled; narrower, and the walker
overwrites the document it is painting. `TB_MARGIN` names the worst case: 128 x
96 is the widest mode the VDP offers, so a dense screenful is 12,288 bytes.

| `char_buffer` | reserve | free pool | gap, old split | against 12 KiB |
|---:|---:|---:|---:|---|
| 253,952 | 1/4 | 63,488 | 21,163 | 1.72x |
| 253,952 | 1/7 | 36,279 | 12,093 | on the line |
| 253,952 | 1/8 | 31,744 | 10,581 | short |
| 126,976 | 1/4 | 31,744 | 10,581 | short |

So a quarter of 248 KiB was the tightest ratio with real headroom, and 128 KiB
could not use a quarter at all. That is what stopped the buffer shrinking.

**A walker moves by number now.** `wline_` is its line and `woff_` is the byte
offset of that line's start, and the bytes are found where they lie rather than
brought to the gap — see `.internal/docs/WALKER.md`. The gap has one job again,
which is holding an insert, and `cb_rebalance` gives it an eighth and the ends
the rest.

The reserve stays a quarter, for reasons that have nothing to do with the old
floor. The first table in section 3 is the whole argument: opening and seeking
both want the reserve, and only scrolling wants it back. Going from 1/4 to 1/16
gains 0.06 s on a 3,000 line scroll and loses 0.52 s on a seek and 0.16 s on an
open.

What changed is that it is now a number that can be argued with. Before, any
answer below a seventh was silent corruption on a wide screen.

## 5. Why not a smaller buffer

Halving `mem_kb` to 128 is allowed now — the floor that forbade it is gone —
and the measurements still say no, for one reason instead of two.

**Arrow scrolling costs twice as much.** 70 cs against 38 for 3,000 lines. Open
and seek are within a few centiseconds either way, so this is the whole of the
trade, and it is on the emulator, where a slide's disk traffic is nearly free.
On a real card the gap is wider. Opening a file happens once; scrolling happens
all day.

What 128 KiB buys is 139 KB of heap, and nothing needs it yet. The reason to
want it is a second open document, which section 7 costs out.

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

So a second document means a smaller buffer, and until recently a smaller
buffer meant a bigger reserve, and a bigger reserve meant a smaller window. The
chain ended at 96 KiB per document with barely 57 KiB of window for two 16 KiB
margins to sit inside — and the 12 KiB gap floor was what held it up.

That floor is gone. It was never a fact about the machine: it came from the
widest mode the VDP can be put in, and a walker having to travel a screenful
through the gap without catching the cursor.

Sizing the floor from the real `rows_ * cols_` would have worked — at 80 x 60 a
dense screenful is 4,800 bytes rather than 12,288 — and it would have made how
many documents can be open depend on the video mode, which is a strange thing
for an editor to do: a user at 1024 x 768 would be told to lower their
resolution before they could open a second file. Removing the floor was the
alternative, and it is what `.internal/docs/WALKER.md` describes and what the
code now does.

So the buffer can be any size the heap allows, at any video mode. What is left
is the ordinary trade in section 5: at 128 KiB a long scroll costs twice what
it costs at 256, and that buys 139 KB of heap. Whether a second open document
is worth that is a product question rather than an arithmetic one, and nothing
in the code decides it any more.

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
