# AED: Another Text Editor

AED is a text editor for the Agon platform (Agon light, Agon light2, Agon Origins, Console8).
It's screen navigation was inspired by various text editors I have used (notepad / nano / kate / vim) and
that inspiration drove the design and choices of navigation keys.

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

[colours]
# Text and background colour, as Agon colour numbers. These were
# taken from the colours your Agon was already using.
fg = 15
bg = 0
```

| Section | Setting | Meaning |
|---|---|---|
| `[editor]` | `tab` | how wide a tab renders, in columns. Values outside 1-16 are pinned to the nearest allowed width. |
| `[colours]` | `fg` | text colour, as an Agon colour number. |
| `[colours]` | `bg` | background colour. |

A setting only counts inside the section that owns it -- a bare `tab = 8` with no
`[editor]` above it is ignored. Section and setting names are matched without regard to
case, spaces around them are ignored, and `#` or `;` starts a comment anywhere on a
line. Anything AED does not recognise is skipped rather than rejected, so a file
written for a newer version still works with an older one, and a setting with a missing
or malformed value keeps its default instead of making the whole file fail.

Changing the colour scheme from inside the editor (`CTRL+ALT+C`) writes the new colours
back to this file, and leaves the rest of it -- your comments, spacing and any settings
this version does not know about -- exactly as you wrote it. Every other setting still
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

# Navigation and shortcuts.
You navigate using the `LEFT, RIGHT, UP, DOWN` arrow keys to move the cursor one character at a time. The cursor will wrap around lines if you
try to move past the end or beginning. You can also use `CTRL+LEFT` and `CTRL+RIGHT` to navigate between white spaces (words) for
faster movement.

Use `PAGE_UP / PAGE_DOWN` to move a page of text at a time.

`DELETE` and `BACKSPACE` keys work as expected, removing characters under the cursor (`DELETE`) and to the left of the cursor (`BACKSPACE`).
If at the end of the line, `DELETE` will merge the next line with the current one.

You can press `CTRL+D` or `CTRL+DELETE` to delete a whole line.

`TAB` inserts a real tab. Tabs are stored in the file as tab characters and are only
expanded to the next tab stop when drawn, so a file's tabs survive being opened and
saved. The tab width is 4 columns by default and can be changed in the settings file
(see **Configuration** below). `LEFT/RIGHT` move over a tab in one step,
since it is a single character, and the cursor sits at the column where the tab
begins.

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
- [ ] File selection while in the editor.
- [ ] Copy-cut-paste.
- [ ] Find.

## Roadmap after v1.0

- [ ] Undo / Redo.
- [x] ~~Native tabs.~~
- [x] ~~Configurable tab size.~~
- [ ] Change settings from inside the editor.
- [ ] Tab-to-space conversion.
- [ ] Syntax highlighting for BBCBasic and assembly files.
- [ ] Unlimted file size support.
- [ ] Console8 mouse support (need to get one).
