#!/bin/bash
# Do the documents still point at what they say they point at?
#
# Three kinds of link rot, all of them silent:
#
#   * a heading is renamed and the index still links to the old anchor
#   * a file is moved and a link in a document still names the old path
#   * a function moves and a `#L123` deep link lands on a blank line, a brace,
#     or somebody else's code
#
# The third is the one that happens without anyone touching the document, which
# is what makes it worth a test: docs/DESIGN.md links into the sources by line,
# and three of those had already drifted by the time it was first committed.
#
# A deep link is checked for pointing at something that looks like a definition
# rather than at the exact right one -- a blank line, a closing brace or a
# comment body means it has certainly drifted. Landing on the wrong function is
# beyond this, and reading the line in the failure message is how that gets
# caught.
#
# Needs python3. Skipped, not failed, without it.
set -uo pipefail
cd "$(dirname "$0")/.."

if ! command -v python3 >/dev/null; then
    echo "SKIP  docs: python3 not on PATH"

    exit 0
fi

python3 - <<'PY'
import os
import re
import sys

# Everything the repo ships. .internal is gitignored notes and not part of it.
docs = []
for root, dirs, files in os.walk('.'):
    dirs[:] = [d for d in dirs
               if d not in ('.git', '.internal', 'obj', 'bin', 'node_modules')]
    for f in files:
        if f.endswith('.md'):
            docs.append(os.path.normpath(os.path.join(root, f)))
docs.sort()


def slug(heading):
    """GitHub's anchor: lowercased, punctuation dropped, spaces to hyphens.

    Underscores survive -- GitHub keeps them, so `ctrl_pause_frames` anchors as
    itself. Dropping them here rejected a heading that works, which is the worse
    way for this check to be wrong: a false alarm gets the link "fixed" into one
    that does not."""
    return re.sub(r'[^a-z0-9 _-]', '', heading.lower()).replace(' ', '-')


anchors = 0
paths = 0
deep = 0
bad = []

for doc in docs:
    text = open(doc, encoding='utf-8').read()
    here = os.path.dirname(doc) or '.'
    headings = {slug(h) for h in re.findall(r'^#+ (.+)$', text, re.M)}

    for target in re.findall(r'\]\(#([^)]+)\)', text):
        anchors += 1
        if target not in headings:
            bad.append(f'{doc}: #{target} matches no heading')

    for target in re.findall(r'\]\((?!https?:|#)([^)#]+)(#L\d+)?\)', text):
        rel, line = target
        full = os.path.normpath(os.path.join(here, rel))
        if not os.path.exists(full):
            paths += 1
            bad.append(f'{doc}: {rel} does not exist')
            continue
        if not line:
            paths += 1
            continue

        deep += 1
        n = int(line[2:])
        src = open(full, encoding='utf-8', errors='replace').read().split('\n')
        if n > len(src):
            bad.append(f'{doc}: {rel}#L{n} is past the end of a '
                       f'{len(src)} line file')
            continue
        got = src[n - 1].strip()
        if not got or got in ('}', '{', '};') or got.startswith(('//', '*', '/*')):
            bad.append(f'{doc}: {rel}#L{n} points at "{got[:40]}"')

name = f'{len(docs)} documents, {anchors} anchors, {paths} paths, {deep} deep links'
if bad:
    for b in bad:
        print(f'FAIL  {"link rot":<52} {b}')
    print(f'FAIL  {"documents point at what they name":<52} '
          f'{len(bad)} of {anchors + paths + deep} links rotted')
    sys.exit(1)

print(f'PASS  {"documents point at what they name":<52} {name}')
PY
