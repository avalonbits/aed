#!/bin/bash
# Writes the benchmark corpus: a document of the shape AED is actually used on.
#
# Deterministic, so two runs measure the same work. Lines vary in length the
# way source does -- a few long ones, many short -- because the line index and
# the range walk both care about the distribution and not just the total.
set -euo pipefail
out=${1:-bench.txt}
lines=${2:-2000}
python3 - "$out" "$lines" <<'PY'
import sys
out, n = sys.argv[1], int(sys.argv[2])
words = ("the quick brown fox jumps over a lazy dog while editing text on an "
         "eight bit machine with very little memory to spare").split()
with open(out, "w", newline="") as f:
    for i in range(n):
        if i % 17 == 0:
            f.write("\r\n")
            continue
        k = 3 + (i * 7) % 60
        line = " ".join(words[(i + j) % len(words)] for j in range(k // 5 + 1))
        f.write(line[:k] + "\r\n")
PY
wc -c "$out"
