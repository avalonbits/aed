#!/bin/bash
# Builds the benchmark: AED's modules with bench.c's main in place of main.c.
#
# AgonDev's makefile fixes SRCDIR to src and computes the object list from it,
# so the sources are staged into src/ here rather than pointed at where they
# live. Nothing in this directory is part of the editor build -- the benchmark
# links printf, which AED deliberately does not.
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$(cd ../.. && pwd)

if ! command -v agondev-config >/dev/null; then
    echo "build.sh: agondev-config not on PATH" >&2

    exit 1
fi

rm -rf src obj && mkdir -p src
for f in "$ROOT"/src/*.c "$ROOT"/src/*.h; do
    [ "$(basename "$f")" = main.c ] && continue
    cp "$f" src/
done
cp bench.c src/

make clean >/dev/null 2>&1 || true
make "$@"
ls -l bin/aedbench.bin
