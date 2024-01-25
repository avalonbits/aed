# AED: Another Text Editor

AED is a text editor for the Agon platform (Agon light, Agon light2, Agon Origins, Console8).
It's screen navigation was inspired by various text editors I have used (notepad / nano / kate / vim) and
that inspiration drove the design and choices of navigation keys.

Currently it is limited to reading and writing files up to 256KB long with up to 8k lines.

The editor can work in any Agon supported resolution and will use whatever color scheme you've configured
your Agon.

`NOTE: Since release v0.13.0, VDP 1.04 or above is required.`

# Installation

Copy the `aed.bin` file to your sdcard's `/mos` directory, You should now be able to run it just typing `aed` at the command line.

> NOTE: The editor uses most of the memory available, so do not start it if you are in BBCBasic.

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
- [ ] Native tabs / tab-size / tab-to-space.
- [ ] Syntax highlighting for BBCBasic and assembly files.
- [ ] Unlimted file size support.
- [ ] Console8 mouse support (need to get one).
