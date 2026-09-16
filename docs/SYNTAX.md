# Writing a grammar

A grammar teaches AED a language: which files it applies to, and how a line of
that language divides into comments, strings, keywords and the rest. It is an
INI file in `/config/aed/syntax/`, and you can write one on the Agon itself with
AED.

This is the how-to. [`THEMES.md`](THEMES.md) is the other half — what colour
each kind of thing comes out — and [`COLOURING.md`](COLOURING.md) is why the
format is what it is.

## Contents

* [1. The shortest grammar that works](#1-the-shortest-grammar-that-works)
* [2. `[syntax]`](#2-syntax)
* [3. `[match]`, and the six verbs](#3-match-and-the-six-verbs)
    * [3a. `eol`](#3a-eol)
    * [3b. `span`](#3b-span)
    * [3c. `words`](#3c-words)
    * [3d. `bol`](#3d-bol)
    * [3e. `number`](#3e-number)
    * [3f. `label`](#3f-label)
* [4. Order matters](#4-order-matters)
* [5. Scope names](#5-scope-names)
* [6. Case, and what a word is](#6-case-and-what-a-word-is)
* [7. A worked example](#7-a-worked-example)
* [8. When a grammar does not load](#8-when-a-grammar-does-not-load)
* [9. The limits](#9-the-limits)

---

## 1. The shortest grammar that works

Save this as `/config/aed/syntax/lua.cfg`:

```ini
[syntax]
name       = Lua
extensions = .lua

[match]
comment.line     = eol '--'
string.quoted    = span '"' '"' escape \
keyword.control  = words if then else end for while do return local function
constant.numeric = number
```

Open a `.lua` file and it is coloured. **Write the file, reopen the document** —
AED reads the syntax directory every time a file is opened, so there is nothing
to restart and nothing to register.

You also need a theme that covers your background, or AED colours nothing. The
three shipped ones cover all sixteen standard backgrounds, so unless you have
changed them you already have one.

## 2. `[syntax]`

| setting | meaning |
|---|---|
| `extensions` | which files this grammar claims. **Required.** |
| `name` | what the language is called. Up to 23 characters. |
| `case` | `insensitive` to ignore case in `words` and literals. |
| `wordchars` | extra characters that count as part of a word. |

`extensions` is a list separated by spaces or commas, each written with its dot:

```ini
extensions = .c .h .cc .cpp .hpp
```

Matching ignores case, so `.C` and `.ASM` are claimed too. **AED uses the first
grammar in the directory whose list contains the extension**, so two grammars
claiming `.inc` is a race rather than a merge — give one of them the extension
and not the other.

A file with no extension is never claimed, and neither is one whose only dot is
in a directory name.

## 3. `[match]`, and the six verbs

Every line in `[match]` is one rule:

```
<scope name> = <verb> [arguments]
```

The scope name on the left says *what kind of thing this is* and decides its
colour (section 5). The verb on the right says *how to recognise it*.

A literal is written bare, or in single or double quotes when it contains a
space or a quote of the other kind. **A literal is at most 4 characters** —
which covers every comment and string marker in practice.

### 3a. `eol`

```ini
comment.line = eol '//'
```

The literal, and everything after it to the end of the line. This is how nearly
every language writes a line comment: `//`, `;`, `#`, `--`, `REM`.

### 3b. `span`

```ini
comment.block        = span '/*' '*/' multiline
string.quoted.double = span '"' '"' escape \
```

From the opening literal to the closing one. Two modifiers, in either order and
both optional:

* **`escape <c>`** — the character that makes the next one ordinary, so `"a\"b"`
  is one string rather than two.
* **`multiline`** — the span may run past the end of the line and carry on into
  the next.

**`multiline` is the expensive one, and the only one that is.** It is what makes
a block comment stay coloured across a line break, and it is the only reason AED
has to remember anything about a line other than the one it is painting. A
grammar with no multiline span costs nothing between keystrokes. Use it for
block comments; leave it off strings, so one stray quote spoils one line rather
than the rest of the file.

A span with no `multiline` closes at the end of its line whether or not the
closing literal turned up.

### 3c. `words`

```ini
keyword.control = words if else for while do switch case break continue return
```

Any of the listed words, matched **whole**: `format` does not match `for`. The
list is separated by spaces, and one rule can hold as many as fit the grammar's
word budget (section 9).

### 3d. `bol`

```ini
keyword.control.preprocessor = bol '#'
```

The literal, but only at the very start of a line, and then the rest of the
line. This is how C's preprocessor directives are coloured as whole lines.

### 3e. `number`

```ini
constant.numeric = number
```

A numeric literal. Built in rather than spelled out, because every language
wants one and none of them write it the same way. What counts: a leading digit,
or one of `$`, `%`, `#`, `&` followed by digits — which covers `255`, `0xFF`,
`$FF`, `%1010` and `&FF`. The tail of a word is not a number, so the `1` in
`x1` is left alone, and a bare `$` is not a number either.

### 3f. `label`

```ini
entity.name.label = label
```

A word at the very start of a line — no literal, and no argument. Assembly puts
labels in the first column and nothing else, so this is the whole rule. A line
starting with a digit is not a label.

## 4. Order matters

**Rules are tried in order, and the first to match at a position wins.**

Put the rules that swallow text first — comments and strings — because
everything inside one is ordinary text as far as the language is concerned:

```ini
[match]
comment.line         = eol '//'
comment.block        = span '/*' '*/' multiline
string.quoted.double = span '"' '"' escape \
keyword.control      = words if else return
constant.numeric     = number
```

With `words` before the comment rules, `// return early` would colour `return`
as a keyword in the middle of a comment.

## 5. Scope names

The name on the left of each rule is a **TextMate scope name** — the same
vocabulary TextMate, VS Code and Sublime use. AED collapses it onto one of nine
classes by its **first component**, so you do not have to know AED's list to
write a rule that works:

| a scope starting with | becomes | a theme names it |
|---|---|---|
| `comment.` | comment | `comment` |
| `string.` | string | `string` |
| `constant.` | number | `number` |
| `keyword.` | keyword | `keyword` |
| `storage.` | type | `type` |
| `support.` | type | `type` |
| `entity.` | label | `label` |
| `punctuation.` | operator | `operator` |
| anything else | text | `text` |

One exception: **`keyword.control.preprocessor` exactly** becomes `preproc`,
which has a colour of its own.

Two reasons to write the long name rather than the short class:

* A theme written by somebody who has used another editor reads the way they
  expect.
* A scope AED has never heard of renders as plain text instead of failing, so a
  grammar converted from a real `.sublime-syntax` degrades rather than breaks.

`support.` mapping to `type` is worth knowing if you write a grammar for a
language with no types of its own: BASIC uses it for built-in functions, which
leaves the type colour doing useful work.

## 6. Case, and what a word is

```ini
case      = insensitive
wordchars = $%.
```

`case = insensitive` applies to `words` and to every literal, which is what
BASIC and most assemblers want: `PRINT`, `Print` and `print` are one keyword.

`wordchars` adds characters that count as part of a word, written **run
together** with no separator. It affects two things: which characters `words`
and `label` will run through, and where a whole-word boundary falls. Assembly
wants `$` and `%` so that `$FF` is one word; a language with dotted names wants
`.`.

Up to 7 characters. Letters, digits and `_` are always word characters and do
not need listing.

## 7. A worked example

A grammar for eZ80 assembly, built a rule at a time.

**Comments first**, because everything inside one is just text:

```ini
[syntax]
name       = asm
extensions = .s .asm .inc .z80
case       = insensitive
wordchars  = $%

[match]
comment.line = eol ';'
```

**Then strings**, which also swallow text. No `multiline`: a stray quote should
spoil one line, not the file.

```ini
string.quoted.single = span "'" "'" escape \
string.quoted.double = span '"' '"' escape \
```

**Then labels**, before the keyword rules — a label sits in the first column and
may be spelled the same as an instruction:

```ini
entity.name.label = label
```

**Then the language's words.** Splitting them across rules is how they get
different colours: instructions read as keywords, directives as types.

```ini
keyword.control = words ld jp jr call ret push pop add sub inc dec cp
storage.type    = words db dw dl equ org include incbin align
```

**And numbers last**, since the earlier rules have claimed everything that could
be confused with one:

```ini
constant.numeric = number
```

`wordchars = $%` is what makes `$FF` and `%1010` come out as single numbers
rather than a sigil followed by something else.

## 8. When a grammar does not load

AED reports nothing — a grammar that will not load is skipped and the next is
tried — so the symptom is always "my file is not coloured". In order of
likelihood:

1. **No theme covers your background.** A grammar with no theme to colour it by
   is dropped, because dividing a line into tokens and painting them all one
   colour is the work without the result. See [`THEMES.md`](THEMES.md).
2. **The extension is not in the list**, or another grammar claimed it first.
3. **The file is too long.** A grammar must fit in 4,096 bytes, comments
   included, and is refused outright if it does not — half a grammar colours
   some of a language and not the rest, which reads as a bug in the grammar
   rather than a file that did not fit. Cut the comments first.
4. **The file is not where AED looks.** It must be directly in
   `/config/aed/syntax/`.

If the file loads but a particular rule does nothing:

* **A verb AED does not know means that rule is skipped**, silently, and the
  rest of the grammar still loads. Check the spelling against section 3.
* **A rule above it matched first.** See section 4.
* **A `words` rule with no words**, or an `eol`, `bol` or `span` missing its
  literal, is skipped the same way.
* **More than 12 rules**: the ones past the twelfth are ignored. Merge rules
  that share a colour — two `words` lines with the same scope can be one line.

## 9. The limits

| | |
|---|---|
| file size | 4,096 bytes, comments included |
| rules | 12 |
| all `words` text | 1,024 bytes, packed |
| words in total | 224 |
| a literal | 4 characters |
| `name` | 23 characters |
| `extensions` | 63 characters, all told |
| `wordchars` | 7 characters |
| a coloured row | 512 bytes, and 64 runs |

The row limits are the only ones that degrade rather than refuse: past 512 bytes
a line paints in the document's own colour, and past 64 runs the last one
swallows the rest of the row. Both need a line far longer than a screen.
