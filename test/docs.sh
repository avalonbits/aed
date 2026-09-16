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
# A deep link is checked against the symbol it names. Every one of them is
# written [`thing`](../src/file.c#L123), so the link says what it is pointing at
# and the check can find where that actually is. Landing on the wrong function
# used to be beyond this -- four links in DESIGN.md had drifted onto a `case`
# label, a `break;` and a different function, and all four passed a check that
# only asked whether the line looked like code.
#
# `./test/docs.sh --fix` rewrites the line numbers instead of reporting them,
# which is the point: a deep link rots whenever anything above it moves, so the
# repair has to cost less than the rot or the links get deleted instead.
#
# Needs python3. Skipped, not failed, without it.
set -uo pipefail
cd "$(dirname "$0")/.."

if ! command -v python3 >/dev/null; then
    echo "SKIP  docs: python3 not on PATH"

    exit 0
fi

python3 - "$@" <<'PY'
import os
import re
import sys

FIX = '--fix' in sys.argv[1:]

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


def find_def(path, sym):
    """Where `sym` is defined in `path`, 1-based, or None.

    Four shapes, because four are what the sources hold: a macro, a struct
    typedef, a function definition at column 0, and a struct member. A
    declaration is skipped -- it ends in a semicolon, and a reader following a
    link wants the code rather than the promise of it."""
    name = sym.rstrip('()')
    if not re.fullmatch(r'\w+', name):
        return None
    src = open(path, encoding='utf-8', errors='replace').read().split('\n')
    q = re.escape(name)

    for i, l in enumerate(src):
        if re.match(r'^#\s*define\s+' + q + r'\b', l):
            return i + 1
    for i, l in enumerate(src):
        if re.match(r'^typedef\s+struct\s+_?' + q + r'\s*\{', l):
            return i + 1
    for i, l in enumerate(src):
        if (re.match(r'^[A-Za-z_][^;=]*\b' + q + r'\s*\(', l)
                and not l.rstrip().endswith(';')):
            return i + 1
    for i, l in enumerate(src):
        if re.match(r'^\s+[A-Za-z_][\w \*]*\b' + q + r'\s*[;\[:]', l):
            return i + 1

    return None


# Every deep link, by the symbol its text names: [`thing`](path#L12).
DEEP = re.compile(r'\[`([^`]+)`\]\((?!https?:|#)([^)#]+)#L(\d+)\)')

anchors = 0
paths = 0
deep = 0
bad = []
fixed = []

for doc in docs:
    text = open(doc, encoding='utf-8').read()
    here = os.path.dirname(doc) or '.'
    headings = {slug(h) for h in re.findall(r'^#+ (.+)$', text, re.M)}

    # The strong check: a link whose text names a symbol must point at where
    # that symbol is. Rewritten in place under --fix, because the number is
    # derived and a derived number should not be maintained by hand.
    def repoint(m):
        sym, rel, at = m.group(1), m.group(2), int(m.group(3))
        full = os.path.normpath(os.path.join(here, rel))
        if not os.path.exists(full):
            return m.group(0)       # the path check below reports this
        want = find_def(full, sym)
        if want is None:
            # The symbol is not in the file any more, so the number cannot be
            # right and cannot be worked out either. A rename or a deletion;
            # both need a person.
            bad.append(f'{doc}: {rel}#L{at} names {sym}, which is not in '
                       f'{os.path.basename(rel)} at all')

            return m.group(0)
        if want == at:
            return m.group(0)       # already right
        if FIX:
            fixed.append(f'{doc}: {rel}#L{at} -> #L{want}  ({sym})')

            return f'[`{sym}`]({rel}#L{want})'
        bad.append(f'{doc}: {rel}#L{at} is not where {sym} is '
                   f'(it is at L{want})')

        return m.group(0)

    moved = DEEP.sub(repoint, text)
    if FIX and moved != text:
        open(doc, 'w', encoding='utf-8').write(moved)
        text = moved

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
if FIX:
    for f in fixed:
        print(f'FIXED {"":<52} {f}')
    print(f'      {"deep links repointed at what they name":<52} '
          f'{len(fixed)} moved, {deep} checked')
    # And then the ordinary report, because a repair that cannot be made is
    # still a failure. Exiting 0 here would let --fix say success over a link
    # naming something that no longer exists.
if bad:
    for b in bad:
        print(f'FAIL  {"link rot":<52} {b}')
    print(f'FAIL  {"documents point at what they name":<52} '
          f'{len(bad)} of {anchors + paths + deep} links rotted')
    sys.exit(1)

print(f'PASS  {"documents point at what they name":<52} {name}')
PY
