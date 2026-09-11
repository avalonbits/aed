/*
 * Copyright (C) 2023  Igor Cananea <icc@avalonbits.com>
 * Author: Igor Cananea <icc@avalonbits.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _BOOTFONT_H_
#define _BOOTFONT_H_

// Which font the machine was booted into, if the boot script says so.
//
// AED can change the font, and when it exits it has to put back what it found.
// It cannot ask: the VDP's font API has no command that reports which font is
// selected -- the only thing any font operation sends back is the screen
// geometry, which says nothing about identity. So the one name AED has for
// "the font to go back to" is 65535, the system font, and selecting that on the
// way out replaces a font set at boot with the stock 8x8.
//
// A font is selected by *buffer id*, and the id exists in exactly one place a
// program can read: the boot script that selected it. So that is where this
// looks.
//
// This is declared intent rather than truth. It finds the two ways a boot
// script says "use this font" and nothing else -- a font set by any other
// program, or after boot, is invisible, and then AED behaves as it did before.
// The alternative was to infer from the cell size, which says a font is loaded
// but never which one, and so cannot restore anything.

// Where MOS looks for the boot script.
#define BOOTFONT_PATH "/autoexec.txt"

// Longest boot script scanned. Anything past this is not read: a boot script is
// a handful of lines, and reading an arbitrary file whole to find a number in
// it is not worth the memory.
#define BOOTFONT_MAX 4096

// The buffer id the script last selected a font from, or -1 for "it did not",
// which includes selecting the system font. The last selection wins, because
// that is the one in force by the time AED runs.
//
// Recognised, in the two forms a boot script can take:
//
//     fontctl 100                 the Console8 moslet
//     VDU 23 0 149 0 100; 0       the same command, written out
//
// MOS's own VDU command is what makes the second form possible, and it accepts
// spaces or commas between values, a ';' suffix for a 16-bit value, and an 'H'
// suffix for hex -- so `VDU 23,0,95H,0,100;,0` is the same line again.
int bootfont_scan(const char* text, int len);

// The same, reading the boot script from `path`. Returns -1 when there is no
// such file, which is the ordinary case.
int bootfont_read(const char* path);

#endif  // _BOOTFONT_H_
