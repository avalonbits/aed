# Colouring the document

Syntax highlighting on a machine with an 18.4 MHz eZ80 and the screen at the
far end of a serial link. [`DESIGN.md`](DESIGN.md) is the overview; this is the
one subsystem in full.

Two things shape all of it. **A colour change costs bytes on the wire**, so the
number of them matters as much as the number of characters. And **a repaint has
a deadline** — it happens between one keystroke and the next.

## Contents

* [1. Why not regular expressions](#1-why-not-regular-expressions)
* [2. The grammar](#2-the-grammar)
    * [2a. Scope names, and why they are kept](#2a-scope-names-and-why-they-are-kept)
* [3. Themes](#3-themes)
* [4. The lexer](#4-the-lexer)
* [5. The problem that makes this hard](#5-the-problem-that-makes-this-hard)
* [6. The model](#6-the-model)
* [7. The inversion: the screen asks](#7-the-inversion-the-screen-asks)
* [8. What it costs](#8-what-it-costs)
* [9. How it is checked](#9-how-it-is-checked)

---

## 1. Why not regular expressions

TextMate's `.tmLanguage`, VS Code's `.tmLanguage.json` and Sublime's
`.sublime-syntax` differ in how they are written down. They do not differ in
what they are: all three match with **Oniguruma regular expressions**, and the C
grammar VS Code ships is about a hundred patterns.

Running a regex engine at every position of every visible line, on every
repaint, at 18.4 MHz, is not something this machine can do. The engine alone
would be a large fraction of an 88 KB binary, and there is no link-time
optimisation here to win any of it back.

So the choice was never *which format*. It was **where the regular expression
gets resolved** — on a PC before the file reaches the SD card, or never.

**Never.** AED reads a format of its own that keeps the other three's *model*
and *vocabulary*. A converter from a real `.sublime-syntax` can be written later
as a host tool without the editor changing. Starting here rather than with the
converter means a grammar can be written and fixed **on the Agon itself**, which
is the machine the editor is for.

## 2. The grammar

Sublime's model — a match that yields a scope — with TextMate's scope names,
serialised as INI because the settings parser already reads INI.

```ini
# /config/aed/syntax/c.cfg
[syntax]
name       = C
extensions = .c .h .cc .cpp .hpp

[match]                       # tried in order; first to match at a position wins
comment.line                 = eol '//'
comment.block                = span '/*' '*/' multiline
string.quoted.double         = span '"' '"' escape \
keyword.control.preprocessor = bol '#'
keyword.control              = words if else for while do switch case break
storage.type                 = words int char long void struct union enum
constant.numeric             = number
```

Six verbs, no regular expressions:

| verb | matches |
|---|---|
| `eol <lit>` | the literal, and the rest of the line |
| `span <open> <close>` | open to close |
| `words <a b c>` | any of them, on whole-word boundaries |
| `bol <lit>` | the literal, anchored at the start of a line |
| `number` | a numeric literal |
| `label` | a word at the very start of a line, which is where assembly puts one |

and three modifiers: `escape <c>` inside a span, `multiline` on a span, and
`case = insensitive` on the grammar, which BASIC needs and assembly usually
wants. `wordchars` extends what counts as a word character, so `$` and `%` can
belong to an assembly identifier.

`number` is built in rather than expressible as a rule, because every language
wants one and none of them write it the same way. What is accepted is the union
that costs nothing to accept: a leading digit, or one of assembly's sigils, then
the characters numbers are made of. `1st` is not a number and neither is a bare
`$`.

**`span` with `multiline` is the only verb that carries state across a line
break.** Everything else begins and ends inside one line, and that single fact
is what makes sections 5 and 6 as small as they are. A grammar with no multiline
span — assembly, BASIC, INI, which is every shipped grammar but C — skips all of
it: [`syn_crosses_lines()`](../src/lexer.c#L722) answers that question once and
the model costs nothing.

### 2a. Scope names, and why they are kept

AED does not use `comment.line.double-slash`. It collapses scopes by their first
component onto nine token classes — `comment.*` to `TOK_COMMENT`, `string.*` to
`TOK_STRING`. Keeping the long names anyway is not tidiness:

* A theme keys off scope names, so keeping them is what lets a theme be
  recognisable to somebody who has written one before.
* A converter from `.sublime-syntax` needs somewhere to land its scopes. If AED
  invented its own names, the converter would need a mapping table that goes
  stale.

A grammar naming a scope AED has never heard of renders as plain text rather
than failing to load, which is what makes an imported grammar degrade instead of
break.

## 3. Themes

A theme is a colour per class and the list of backgrounds its author meant it
for:

```ini
[theme]
name   = dark
covers = 0 1 4 5

[colours]
comment = 8
string  = 10
type    = 14
```

A colour that reads well on black is unreadable on white, so **the background in
force picks the theme**. [`ed_pick_syntax()`](../src/editor.c#L178) chooses the
grammar by the document's extension and then the first theme that covers the
background, and it runs at startup, on open, and when the settings modal leaves
a different background behind.

**A theme is a view of a document, not a setting.** A theme may move the pair
the document is drawn on, and it moves only the *active* pair; what the reader
chose is untouched and is what the settings file keeps. Opening a file no
grammar claims puts their colours back. This is the rule the whole feature was
asked to respect, and it is the one the tests guard hardest.

A grammar with no theme for the background in force is **dropped rather than
used**. Dividing a line into tokens and painting every one of them the same
colour is the work without the result.

## 4. The lexer

[`syn_lex()`](../src/lexer.c#L570) takes one row of text, the state it begins
in, and returns the runs and the state it leaves.

```
    syn_lex(grammar, line, len, state_in, &state_out, runs, max)
```

A row comes back as a handful of [`tok_run`](../src/syntax.h#L63) — each a class
and the column it ends at — rather than a colour per column. That is the shape
the painting wants: it walks the columns and emits a colour change only when it
crosses a run boundary.

[`add_run()`](../src/lexer.c#L221) merges neighbouring runs of the same class,
and lets the last run swallow the rest when it runs out of room. Overflow
therefore costs **colour rather than correctness** — and since the cost of a
colour change is why a cap is wanted at all, a row that overflows the cap is a
row that was about to be expensive.

Everything the lexer touches is bounded: 12 rules, 1,024 bytes of packed word
text, 224 word offsets, 512 bytes of one row. A grammar file that does not fit
is **refused rather than half read**, because half a grammar is a grammar that
silently colours some things and not others.

## 5. The problem that makes this hard

A block comment runs from one line into the next, so painting line *N* needs to
know what line *N−1* ended inside.

Storing that per line of the *document* does not bound: since
[`PAGING.md`](PAGING.md) the document is whatever size the file is, so the state
would grow with the file on a machine that has no room to grow anything. And it
would have to be maintained across every edit, which is the class of bug where a
stored position goes stale against the text it names.

It is not needed, because of two observations:

1. **A screen is painted top to bottom.** Within one repaint the state carries
   from each row to the next for free. What is needed is the state of *one* line:
   the first one on screen.
2. **That is only unknown when the view jumps.** Arrow keys, page up and page
   down move the top line by a known amount from a state already in hand. A jump
   is `CTRL+G`, `CTRL+END`, landing on a find result, or a window slide.

So: **carry the state forward, and read back a bounded distance after a jump.**
[`syn_state_before()`](../src/lexer.c#L735) rescans at most
[`SYN_LOOKBACK`](../src/syntax.h#L222) — 200 lines — which is a trivial lexer
over about 8 KB, and only on a jump. Zero bytes of document-sized state, and
correct unless a span runs longer than the lookback.

## 6. The model

What survives is small: **what each line begins inside, for the lines on
screen**, held as a window.

```c
    char lineSyn_[SCR_MAX_ROWS];   // what line synFirst_ + i begins inside
    int  synFirst_;                // the first line an answer is held for
    int  synKnown_;                // how many consecutive lines from it
    int  synTop_;                  // the document line drawn at the top row
```

**Keyed by document line, not by screen row**, and that is the whole of it. A
line begins inside what it begins inside; which row it is drawn on has nothing
to do with that. Keyed by row, scrolling threw every answer away although the
document had not changed — and the code that chased rows around had cases it
could not express and gave up in, after which the cursor had nothing to consult
and rubbed the colouring out of each cell it crossed.

Four operations, and no state meaning *give up*:

| | |
|---|---|
| [`line_state()`](../src/cmd_ops.c#L668) | inside the window it is a lookup; below it the window grows ([`extend_lines`](../src/cmd_ops.c#L548)); above it the window starts again there ([`refill_lines`](../src/cmd_ops.c#L605)) |
| [`set_line_state()`](../src/cmd_ops.c#L697) | records what a paint has just worked out |
| [`lines_moved()`](../src/cmd_ops.c#L738) | the document gained or lost a line at a point |
| [`line_at_row()`](../src/cmd_ops.c#L775) | `synTop_ + (ypos - topY_)`, the only place a row becomes a line |

A join **renumbers** its answers rather than throwing them out, which is worth
doing: throwing them out costs a screenful of lexing per edit and measured seven
times what a join should cost. A line *appearing* stops the answers there
instead, because the new line sits in the middle of them with no answer of its
own and the window is dense — there is nowhere to record a hole.

Commands say one thing, the same way whatever the edit was: *the view scrolled*,
*lines moved here*, or *this line begins here*. Three statement shapes, against
the dozen paint sites that each used to wire up colour by hand.

## 7. The inversion: the screen asks

The first version **pushed**: each paint site worked out the colours and handed
them to the screen. There are about a dozen such sites, and the ones that forgot
painted plainly with nothing to say they had. Every bug in the feature's first
week was a site that forgot, and fixing one by adding a call at the site was
adding a thirteenth place to forget.

So the screen asks. [`scr_set_colourer()`](../src/screen.c#L1366) hands it two
callbacks, once, at startup:

```c
typedef int  (*scr_colourer)(void* ctx, char ypos, const char* pre, int presz,
                             const char* suf, int sufsz, const tok_run** runs);
typedef char (*scr_cell_colourer)(void* ctx, char ypos, int col);
```

Every path that paints a row asks the first at paint time —
[`ed_colour_row()`](../src/cmd_ops.c#L148) — and every path that shows the
cursor asks the second — [`ed_colour_cell()`](../src/cmd_ops.c#L186). **A row
cannot be painted without the question being asked**, because asking is inside
the painting rather than in front of it.

Three consequences fall out:

* **The answer describes where the cursor is**, rather than where it was when
  something last thought to say. The cursor-colour bug class disappears with the
  push.
* **A paint that does not come through the colourer paints plainly** rather than
  inheriting: the runs are dropped when a row is done, so no row can wear
  another's colouring.
* **The model is read, never asked to check itself.** Asking whether it still
  describes the screen means working that out from the cursor, which during a
  paint is halfway through an edit. Every return and every join decided the
  screen was stale and lexed all of it twice, against a view that had not
  finished moving. The model is brought up to date at the start of an operation,
  where the view and the document agree.

## 8. What it costs

**Lexing a line of C costs about a millisecond and a half.** Every other number
here follows from that one.

Without a model, painting a row means lexing every row above it to find out what
it is inside. On a half-screen cursor that is 28 rows, and it measured **45
milliseconds a keystroke** on an Agon — which is the whole of why the model
exists.

With it, an ordinary keystroke lexes **one line**: the row it changed. The rows
below are repainted only when that row's answer moved, which is almost never,
and a screen painted top to bottom chains the rest for free.

The expensive case is a jump, and it is bounded: at most `SYN_LOOKBACK` lines
read back, once for the view rather than once per row painted. A grammar with
nothing that crosses a line — every shipped grammar but C — pays none of it.

The number worth watching is **screen rebuilds**, and it is zero. The
arrangement this replaced rebuilt the model whenever it could not answer, and it
could not answer often.

## 9. How it is checked

`test/test_highlight.c` drives the whole path, and three habits in it are worth
copying:

* **Assert on the wire, not on the model.** A colour change is `VDU 17, c`, so
  the tests read the byte stream a paint produced and find the colour at a
  column. A test that asks the model what it thinks is a test of the model's
  opinion of itself.
* **Check the standing rule per writer, not per scenario.** Every function that
  writes a row — painting one, writing one, writing one with a selection,
  blanking one — is asserted to ask the colourer. That is what stops a
  thirteenth paint site being added silently.
* **Check rows bottom-up.** Checking them top-down repairs the fault before it
  is checked, because painting a row writes the row below's answer. Several
  early tests passed for that reason and for no other.

A mutation that survives is the signal to look harder. Several of the tests here
exist only because the first version of them passed against deliberately broken
code — a column count that included the cursor's own bytes, a document whose
rows all shared one state, a document shorter than the screen. Each of those
passes was the test agreeing with itself.
