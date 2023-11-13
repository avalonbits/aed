# AED: Another Text Editor

AED is a text editor for the Agon platform (Agon light, Agon light2, Agon Origins, Console8).
It's screen navigation was inspired by various text editors I have used (notepad / nano / kate / vim) and
that inspiration drove the design and choices of navigation keys.

Currently it is limited to reading and writing files up to 256KB long with up to 8k lines.

The editor can work in any Agon supported resolution and will use whatever color scheme you've configured
your Agon.

# Installation

Copy the `aed.bin` file to your sdcard's `/mos` directory, You should now be able to run it just typing `aed` at the command line.

> NOTE: The editor uses most of the memory available, so do not start it if you are in BBCBasic.

# Running the editor.
If you run it just as `aed` it will start the editor using `/aed.txt` as its backing file. If the file can't be created,
it will exit with the message `Quit`. If the file already exists, it will read it into the buffer and display it on the editor screen.

You can specify the file at startup by typing `aed file.name` and it will try to create it, exiting with `Quit` if it can't.
If the file already exists, it will read it into the buffer and display it on the editor screen.

# Navigation and shortcuts.
You navigate using the `LEFT, RIGHT, UP, DOWN` arrow keys to move the cursor one character at a time. The cursor will wrap around lines if you
try to move past the end or beginning. You can also use `CTRL+LEFT` and `CTRL+RIGHT` to navigate between white spaces (words) for
faster movement.

`DELETE` and `BACKSPACE` keys work as expected, removing characters under the cursor (`DELETE`) and to the left of the cursor (`BACKSPACE`).
If at the end of the line, `DELETE` will merge the next line with the current one.

You can press `CTRL+D` or `CTRL+DELETE` to delete a whole line.

Finally, `CTRL+Q` will save the buffer to the specified file on startup (or `/aed.txt` of none was specified) and exit the editor.

# Road to v1.0
The following features will be implemented before releasing v1.0 of the editor:

- [ ] `BACKSPACE` merges current line with previous when pressed at the beginning of the line.
- [ ] Shortcuts to change foreground and background colors.
- [ ] `PAGE-UP` and `PAGE-DOWN` support.
- [ ] Shortcut for saving the current buffer without quiting.
- [ ] File selection while in the editor.

## Roadmap after v1.0
- [ ] Copy-cut-paste.
- [ ] Find.
- [ ] Undo / Redo.
- [ ] Native tabs / tab-size / tab-to-space.
- [ ] Syntax highlighting for BBCBasic and assembly files.
- [ ] Unlimted file size support.
- [ ] Console8 mouse support (need to get one).
