# Benchmark

AED's CPU-bound paths, timed on the machine they run on.

    ./test/bench/mkcorpus.sh test/bench/bench.txt 2000
    ./test/bench/build.sh

Copy `bin/aedbench.bin` and the corpus to a card and run `aedbench /bench.txt`.
Results go to the screen and to `/bench.out`, so a headless emulator run can be
read back off the card.

## Method

From the eZ80 optimisation guide this was written against, and worth following
exactly -- every one of these was learned by getting it wrong:

* **Run the emulator at the real 18.432 MHz.** With `-u` the guest's `clock()`
  measures how fast the host emulated the work, and the number stops meaning
  anything.
* **`clock()` counts hundredths**, so each case repeats until it has tens of
  them and reports the total against the repeat count. The tick boundary falls
  in a different place each time, so the quantisation averages out rather than
  accumulating.
* **One change, one measurement, nothing else running.** Two emulators on one
  host time each other's work.
* **The emulator is deterministic to about 0.25%.** Two runs of this gave 932
  and 934 on the same case. Do not claim a change under half a percent from a
  single run, and do not dismiss a consistent 0.4% as noise.
* **The host is not a proxy.** It is biased rather than noisy, and in the
  direction that flatters the work. Use the host suite for correctness and this
  for speed.

## What it does not measure

**Repainting.** Every byte of that goes down a UART the emulator does not
rate-limit, so a screen test here would report the link as free -- and on real
hardware it is most of what a keystroke costs.

**The card.** With `--sdcard <path>` the host filesystem serves the calls and
MOS's FatFS barely runs, so `load` and `save` here are AED's own work with the
I/O taken out. That makes them a good measure of the parsing and indexing and a
bad measure of what a user waits for.

## Baseline

VDP 2.16.0 / MOS 3.0.2 under fab-agon-emulator 1.2.4, 64,562 bytes over 2,001
lines. Per repeat, in centiseconds:

| case | `9f53dc9` | now | change |
|---|---:|---:|---:|
| load | 287 | 59 | **-79%** |
| find-miss | 233 | 88 | **-62%** |
| walk-lines | -- | 50 | |
| seek-lines | -- | 150 | |
| range-copy | -- | 58 | |
| type | 0.030 | 0.030 | |
| undo | 0.013 | 0.012 | |

seek-lines, walk-lines and range-copy have no first reading because the first
two cases were measuring nothing and the third did not exist: `tb_pos` is
`{line, x}` and the benchmark built it `{x, line}`, so every seek asked for
line 0 and the range was empty. Worth remembering as a warning about
benchmarks -- both cases ran, both reported a number, and the number was of no
work being done.
