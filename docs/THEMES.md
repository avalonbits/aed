# Writing a theme

A theme says what colour each kind of thing is drawn in, and which backgrounds
its author meant it for. It is an INI file in `/config/aed/themes/`, and you can
write one on the Agon itself with AED.

This is the how-to. [`COLOURING.md`](COLOURING.md) is why it works the way it
does, and [`SYNTAX.md`](SYNTAX.md) is the other half — teaching AED a language.

## Contents

* [1. The shortest theme that works](#1-the-shortest-theme-that-works)
* [2. Changing a theme you already have](#2-changing-a-theme-you-already-have)
* [3. `[theme]`](#3-theme)
* [4. `[colours]`](#4-colours)
* [5. Backgrounds, and how yours is chosen](#5-backgrounds-and-how-yours-is-chosen)
* [6. Setting the pair the text is drawn on](#6-setting-the-pair-the-text-is-drawn-on)
* [7. When a theme does not load](#7-when-a-theme-does-not-load)
* [8. The limits](#8-the-limits)

---

## 1. The shortest theme that works

Save this as `/config/aed/themes/mine.cfg`:

```ini
[theme]
name   = mine
covers = 0

[colours]
comment = 8
string  = 10
keyword = 14
```

Open a `.c` file with your background set to colour 0 and it is coloured. That
is the whole cycle: **write the file, reopen the document.** AED reads the
themes directory every time a file is opened, so there is nothing to restart and
nothing to register.

## 2. Changing a theme you already have

Edit the file in place — `aed /config/aed/themes/dark.cfg` — change a number,
save, and reopen your document. The `.cfg` extension is one AED highlights, so
you are editing the theme in colour.

Three habits worth having:

* **Copy before you edit.** A future release may ship a changed `dark.cfg` and
  overwrite yours. A theme under your own name is never touched.
* **Change one colour at a time.** Colour on a screen is judged by eye against
  the text you actually read, and two changes at once are hard to separate.
* **Watch out for your background.** A theme that covers `0` stops applying the
  moment you press `CTRL+E` and pick a different background — see section 5.

## 3. `[theme]`

| setting | meaning |
|---|---|
| `name` | what the theme is called. Up to 15 characters. |
| `covers` | the backgrounds this theme suits, as Agon colour numbers. |
| `fg` | optional: the colour ordinary text is drawn in. |
| `bg` | optional: the background the theme wants. |

`covers` is a list separated by spaces or commas — `covers = 0 1 4 5` and
`covers = 0,1,4,5` are the same thing. Up to 16 numbers, each 0 to 63.

**A theme with no `covers` is never chosen.** That is the one mistake worth
knowing about: everything else can be left out and the theme still works.

## 4. `[colours]`

One line per kind of thing. `[colors]` is accepted as well, so a theme written
either way loads.

| name | what it colours |
|---|---|
| `text` | anything no rule claimed |
| `comment` | comments |
| `string` | string and character literals |
| `number` | numeric literals |
| `keyword` | language keywords |
| `type` | type names, and BASIC's built-in functions |
| `preproc` | preprocessor directives |
| `label` | labels and other declared names |
| `operator` | operators and punctuation |

Each value is an Agon colour number. **A name AED does not recognise is
skipped**, and so is a value that is not a number, so a theme written for a
later version with more classes still loads on this one.

**A class you leave out falls back to `text`.** If you have not set `text`
either, that kind of thing is left in the document's own colour — which is the
usual way to say "leave this alone". `dark.cfg` omits `text` deliberately, so
ordinary code stays in whatever colours you chose and only the parts a rule
claimed are moved.

## 5. Backgrounds, and how yours is chosen

A colour that reads well on black is unreadable on white, so a theme is keyed to
the background rather than picked by name. **AED uses the first theme in the
directory whose `covers` list contains the background in force.**

The three shipped themes divide the sixteen standard backgrounds between them:

| theme | covers |
|---|---|
| `dark.cfg` | 0 1 4 5 |
| `light.cfg` | 3 6 7 11 14 15 |
| `mid.cfg` | 2 8 9 10 12 13 |

Two consequences:

* **Changing your background changes your theme**, immediately, including from
  inside `CTRL+E`.
* **A background no theme covers means no colouring at all.** AED would rather
  leave a file in your own colours than paint it in something unreadable — so if
  you add a theme, give it every background you want it used for.

To take over a background from a shipped theme, list it in your own `covers` and
make sure yours is found first. AED reads the directory in the order the card
returns, which in practice is the order the files were created, so the simplest
reliable answer is to remove the background from the shipped theme's `covers`.

## 6. Setting the pair the text is drawn on

`fg` and `bg` in `[theme]` let a theme change the colours the document itself is
drawn in, rather than only the things rules claimed.

**This never changes your settings.** A theme moves the *active* pair while a
file it covers is open; `/config/aed.ini` keeps the colours you chose, and
opening a file no grammar claims puts them back. A theme cannot make a permanent
change to your editor, which is the rule the whole feature is built around.

The three shipped themes set neither, on purpose: they colour the tokens and
leave your own colours exactly where they are. Set them only if the theme really
is a scheme rather than a palette — and remember that `bg` moving the background
does not change which theme is chosen, because that was decided by the
background you had.

## 7. When a theme does not load

AED reports nothing: a theme that will not load is skipped, and the next one is
tried. That is deliberate — there is nowhere useful to report to on this machine
— so the symptom is always "my file is not coloured". In order of likelihood:

1. **No `covers`, or not the background you are using.** Check with `CTRL+E`
   which background you actually have.
2. **The file is too long.** A theme must fit in 1,024 bytes, comments included,
   and is refused outright if it does not — half a theme colours some classes
   and not others, which reads as a theme written badly. Cut the comments.
3. **The file is not where AED looks.** It must be directly in
   `/config/aed/themes/`, not in a subdirectory.
4. **No grammar claimed the file.** A theme only applies to a document some
   grammar recognises; a `.txt` is never coloured however many themes you have.
   See [`SYNTAX.md`](SYNTAX.md).

A theme that loads but colours nothing usually has its colour names wrong — they
are the nine in section 4, not scope names like `comment.line`.

## 8. The limits

| | |
|---|---|
| file size | 1,024 bytes, comments included |
| `name` | 15 characters |
| `covers` | 16 backgrounds, each 0 to 63 |
| classes | the nine in section 4 |
| themes in the directory | no limit; the first match wins |
