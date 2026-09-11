# AED: Another Text Editor

AED is a text editor for the Agon platform (Agon light, Agon light2, Agon Origins, Console8).
It's screen navigation was inspired by various text editors I have used (notepad / nano / kate / vim) and
that inspiration drove the design and choices of navigation keys.

It selects with `SHIFT` and the movement keys, copies, cuts and pastes with
`CTRL+C`, `CTRL+X` and `CTRL+V` -- between files, not just within one -- and
undoes with `CTRL+Z` and redoes with `CTRL+Y`. It finds text with `CTRL+F`, opens
another file without leaving the editor, keeps real tab characters, and remembers your colours and
tab width in a settings file.

Currently it is limited to reading and writing files up to 248KB long with up to 8k lines.

The editor can work in any Agon supported resolution and will use whatever color scheme you've configured
your Agon.

`NOTE: VDP 1.04 or above is required (since v0.13.0), and MOS 2.2.3 or above.`

# Installation

Copy the `aed.bin` file to your sdcard's `/bin` directory. You should now be able to run it just
typing `aed` at the command line.

> NOTE: The editor uses most of the memory available, so do not start it if you are in BBCBasic.

### Upgrading from an older release

**Delete any old `/mos/aed.bin`.** Earlier releases were installed into `/mos`, and MOS searches
`/mos` before `/bin`, so a leftover copy there will shadow the new one.

`/mos` is no longer supported, and a current build placed there will not merely fail to start — it
will panic immediately:

```
== RST $38 panic. Guru meditation ==
PC:040046
```

MOS treats files in `/mos` as *moslets* and loads them at `0x0B0000`, whereas AED is now linked to
run from `0x040000`. It cannot be built as a moslet either: the moslet area is 64KB and AED needs
about 272KB for its buffers. `/bin` is loaded at `0x040000`, which is why the editor lives there
now.

MOS has searched `/bin` since version 2.2.0, so 2.2 is the lowest usable minor release.
The supported floor is the **last point release** of that line, 2.2.3 — if you are on
2.2.x, be on 2.2.3.

# Configuration

AED keeps its settings in `/config/aed.cfg`, an ordinary INI file. **The first time you
run it, it writes that file for you**, filled in with the settings it is currently
using -- including the colours it picked up from your Agon -- so there is something to
edit rather than a format to guess at.

`/config` sits alongside `/bin` and `/mos` rather than inside them, since those are for
executables. The convention is one file per application, or `/config/<app>/` for an
application that needs several.

A freshly written file looks like this:

```ini
# AED settings.
#
# An INI file: [section] headings, then name = value lines. Blank lines
# are ignored and '#' or ';' starts a comment. Sections and settings AED
# does not recognise are skipped, so this file stays readable by older and
# newer versions alike. Edit and restart AED to apply.

[editor]
# How wide a tab renders, in columns. 1 to 16.
tab = 4

# A font to load at startup: a raw bitmap, 256 glyphs, 8 pixels wide,
# one byte per row. Its height is the file size divided by 256, so a
# 2304-byte file is 9 rows -- an 8-row font with a blank row added, which
# separates the text lines without costing a column.
#
# Needs a VDP with the font API (Console8 2.8.0+). AED cannot check, so
# uncommenting this is what says yours has it.
#font = /config/aed/unscii8x9.bin

[colours]
# Text and background colour, as Agon colour numbers. These were
# taken from the colours your Agon was already using.
fg = 15
bg = 0
```

| Section | Setting | Meaning |
|---|---|---|
| `[editor]` | `tab` | how wide a tab renders, in columns. Values outside 1-16 are pinned to the nearest allowed width. |
| `[editor]` | `font` | path to a font to load at startup. Commented out by default -- see below. |

The settings can also be edited from inside AED with **CTRL+E**, which writes
them back to the file. It changes only what you change: every other line is
copied through as it was, comments and all.

`ctrl_pause_frames` is not offered there, on purpose -- it is the one setting
that can do harm to get wrong, and a row in a list is no place to explain why.
Set it in the file, having read the section below.
| `[colours]` | `fg` | text colour, as an Agon colour number. |
| `[colours]` | `bg` | background colour. |
| `[vdp]` | `ctrl_pause_frames` | how long the VDP pauses when a line wraps while CTRL is held, in frames. Not written by default -- see below. |

### `font`

The stock Agon font is eight pixels tall and uses every one of them: `p`, `y`
and `j` put their tails on the last row, so consecutive lines of text touch.
Nothing can be done about that within an 8x8 cell -- every mechanical way of
freeing a row wrecks the capitals.

The way out is that the VDP's font height is a **parameter**, not a fixed eight.
Take any 8x8 font, add a blank ninth row to every glyph, and the text lines
separate without a single column being given up: 80 columns still, 53 rows
instead of 60. A 16-row font works the same way and gives 30 rows.

Three fonts come with AED, in `fonts/`. Copy one to the card and name it:

```ini
[editor]
font = /config/aed/unscii8x10.bin
```

| file | cell | screen | |
|---|---|---|---|
| `unscii8.bin` | 8x8 | 80x60 | unscii as published: better letterforms than the stock font, but the lines still touch |
| `unscii8x10.bin` | 8x10 | 80x48 | the same with two blank rows added, so the lines do not touch |
| `unscii16.bin` | 8x16 | 80x30 | unscii-16, drawn at sixteen rows, with the gap already in it |

unscii is by Viznut and is public domain. See `fonts/README.md` for how the
padded one is produced and why it exists.

The file is a raw bitmap and has no header: 256 glyphs, 8 pixels wide, one byte
per row, lowest character first. **The height is the file size divided by 256**,
so a 2048-byte file is 8 rows, 2304 is 9, and 4096 is 16. A file whose size is
not a whole number of 256-byte rows is refused, as is one tall enough to leave
fewer than four rows on screen. A font that cannot be loaded is not an error --
AED carries on in whatever font the machine already had.

The font API needs a Console8 VDP 2.8.0 or newer, and **AED checks before it
sends anything**. It asks the VDP to select the font it is already using, which
costs seven bytes and changes nothing; a VDP with the font API answers, and one
without does not. If there is no answer the font is skipped and AED carries on
in the machine's own font.

So this is *not* the bargain `ctrl_pause_frames` asks of you. Setting it says "I
would like this font", not "I promise my VDP is new enough". On an older VDP you
get the stock font and nothing worse.

AED puts the font back when it exits, the same way it puts your colours back.

If your `/autoexec.txt` sets a font -- `loadfont` into a buffer and `fontctl` to
select it, or the same thing written out as a `VDU 23,0,&95,0,<buffer>;` line --
AED reads which buffer it used and selects that on the way out, rather than the
stock font. Without that it has no choice: the VDP has no command that reports
which font is in force, so the only name AED would have for "put it back" is the
system font, which would replace your font with the default every time you quit.

That is declared intent rather than truth. A font set by anything else, or after
boot, is invisible to it, and AED then does what it always did.

### `ctrl_pause_frames`

Holding CTRL while moving right along a line longer than the screen feels
sluggish. That is the VDP, not AED: every time a line wraps with CTRL held it
pauses for three frames. Setting the count to zero stops it.

```ini
[vdp]
ctrl_pause_frames = 0
```

**AED never sends this unless you ask for it, and you should only ask on a
Console8 VDP.** The VDU sequence that carries it is a later addition. A VDP that
does not recognise it reads the four bytes that follow as commands, and one of
them clears the screen. AED supports VDP 1.04 upwards and has no way to ask the
VDP which version it is, so it cannot decide this for you.

The related CTRL+SHIFT pause -- the VDP stops drawing entirely while both are
held -- cannot be switched off at all. AED works around it by never writing the
last column of a row.

A setting only counts inside the section that owns it: `tab = 8` under `[colours]` is
ignored, which is what leaves room for a future section to use a name like `fg` for
something of its own. A setting written *before* any heading is taken at face value, so
a file that predates the sections, or one you edited and forgot the heading on, still
does what it plainly says. Section and setting names are matched without regard to
case, spaces around them are ignored, and `#` or `;` starts a comment anywhere on a
line. Anything AED does not recognise is skipped rather than rejected, so a file
written for a newer version still works with an older one, and a setting with a missing
or malformed value keeps its default instead of making the whole file fail.

Changing the colour scheme from inside the editor (`CTRL+ALT+C`) writes the new colours
back to this file, and leaves the rest of it -- your comments, spacing, and every
setting other than `fg` and `bg` -- exactly as you wrote it. Every other setting still
needs the file edited by hand and AED restarted.

If `/config` cannot be created -- a write-protected card, say -- AED starts normally
with its defaults and simply does not save them.

# Running the editor.
If you run it just as `aed` it will start the editor using `/aed.txt` as its backing file. If the file can't be created,
it will exit with the message `Quit`. If the file already exists, it will read it into the buffer and display it on the editor screen.

You can specify the file at startup by typing `aed file.name` and it will try to create it, exiting with `Quit` if it can't.
If the file already exists, it will read it into the buffer and display it on the editor screen.

# File operations
`CTRL+S` will save the current text buffer to a file. If no file name was provided on startup, a prompt for the file name is shown.
`CTRL+ALT+S` will always show a prompt for the file name before saving.

`CTRL+O` opens another file, prompting for the name at the bottom of the screen with
the current one filled in. If the document you are editing has unsaved changes it asks
whether to save them first, the same way quitting does, and answering `ESC` there calls
the whole thing off. A name that does not exist yet is created, so this is also how you
start a new file without leaving the editor.

Opening replaces the document you are editing -- there is only ever one buffer -- but a
file that cannot be opened, or that is too large to fit, leaves your work exactly where
it was and says what went wrong.

# Navigation and shortcuts.
You navigate using the `LEFT, RIGHT, UP, DOWN` arrow keys to move the cursor one character at a time. The cursor will wrap around lines if you
try to move past the end or beginning. You can also use `CTRL+LEFT` and `CTRL+RIGHT` to navigate between white spaces (words) for
faster movement.

Use `PAGE_UP / PAGE_DOWN` to move a page of text at a time.

# Selecting text
Hold `SHIFT` and move the cursor to select. Every movement key works: the arrows,
`HOME/END`, `PAGE_UP/PAGE_DOWN`, and `CTRL+LEFT / CTRL+RIGHT` to select whole words at a
time. The selected text is shown with the colours reversed, and a line break inside the
selection shows as one highlighted space at the end of the line.

`CTRL+A` selects the whole document.

There is no mode to leave. The selection starts where the cursor was when you first
held `SHIFT`, grows and shrinks as you keep moving, and disappears the moment you press
anything that is not a movement key -- so there is no way to get stuck in it and nothing
to remember.

# Copy, cut and paste
`CTRL+C` copies the selection, `CTRL+X` cuts it, and `CTRL+V` pastes at the cursor.
Copying leaves the selection alone so you can change your mind about where it ends;
cutting and pasting consume it.

Typing with text selected replaces it, as does pasting. `BACKSPACE` and `DELETE` with a
selection just remove it -- there is no undo yet, so a selection is worth a glance before
you type over it.

The clipboard belongs to the editing session rather than the file, so you can copy in
one document, open another with `CTRL+O`, and paste it there. Quitting the editor
forgets it.

There is no limit on how much you can copy. Up to 8KiB is kept in memory; anything
larger is written to a scratch file named after the document -- `notes.txt.scratch`
beside `notes.txt` -- and read back from there when you paste. The file is removed when
you quit, and it holds nothing but the copied text, so a stray one left behind by a
crash can be opened and read like any other file.

If the scratch file cannot be written -- a full or write-protected card -- the copy
fails and says so, and a cut whose copy failed does not delete anything. Losing the text
with nothing to paste would be worse than not cutting.

`DELETE` and `BACKSPACE` keys work as expected, removing characters under the cursor (`DELETE`) and to the left of the cursor (`BACKSPACE`).
If at the end of the line, `DELETE` will merge the next line with the current one.

You can press `CTRL+D` or `CTRL+DELETE` to delete a whole line.

`TAB` inserts a real tab. Tabs are stored in the file as tab characters and are only
expanded to the next tab stop when drawn, so a file's tabs survive being opened and
saved. The tab width is 4 columns by default and can be changed in the settings file
(see **Configuration** below). `LEFT/RIGHT` move over a tab in one step,
since it is a single character, and the cursor sits at the column where the tab
begins.

# Find
`CTRL+F` asks what to look for and jumps to the first match after the cursor,
wrapping round the end of the file. `CTRL+N` finds the next one and `CTRL+P` the
previous, without asking again.

Matching ignores case, and a match never spans a line break.

What was found is selected and placed halfway down the screen, so a match always
appears in the same place rather than somewhere you have to look for -- and near
the top of a file, as close to the middle as the lines above allow. Because it is
selected, typing replaces it.

If there is no match the editor says so and stays exactly where it was -- giving up
on a search leaves you where you started, not somewhere else.

# Undo and redo
`CTRL+Z` undoes the last edit and `CTRL+Y` puts it back.

Edits run together, so a burst of typing undoes as one step rather than a letter
at a time. A run ends when you move the cursor, when you switch between typing,
`DELETE` and `BACKSPACE`, or after 128 characters -- so no single `CTRL+Z` takes
back more than a couple of lines' worth.

Undoing back to the state the file was last saved in clears the `*` in the
footer: the marker means "this differs from the file", not "something happened".

The history is a fixed 16KiB, which is thousands of edits in practice. When it
fills, the oldest are forgotten. A single edit larger than that -- selecting a
whole large document and deleting it, say -- cannot be undone at all, and clears
the history rather than leaving a gap in the middle of it. Opening another file
clears it too.

`CTRL+Q` will save the buffer to the specified file on startup (or `/aed.txt` of none was specified) and exit the editor.
If no file was specified on startup, it will prompt for a file name to save the text buffer.

`CTRL+ALT+C` will show the colour picker at the bottom of the screen. Use `UP/DOWN` to select the foreground color and `LEFT/RIGHT` to
select the background color. 

# Road to v1.0
The following features will be implemented before releasing v1.0 of the editor:

- [x] ~~BACKSPACE merges current line with previous when pressed at the beginning of the line.~~
- [x] ~~Shortcut to change foreground and background colors.~~
- [x] ~~`PAGE-UP` and `PAGE-DOWN` support.~~
- [x] ~~Shortcut for saving the current buffer without quiting.~~
- [x] ~~File selection while in the editor.~~
- [x] ~~Copy-cut-paste.~~
- [x] ~~Find.~~

## Roadmap after v1.0

- [x] ~~Undo / Redo.~~
- [x] ~~Native tabs.~~
- [x] ~~Configurable tab size.~~
- [ ] Change settings from inside the editor.
- [ ] Tab-to-space conversion.
- [ ] Syntax highlighting for BBCBasic and assembly files.
- [ ] Unlimted file size support.
- [ ] Console8 mouse support (need to get one).
