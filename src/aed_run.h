/*
 * Copyright (C) 2026  Igor Cananea <icc@avalonbits.com>
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

#ifndef _AED_RUN_H_
#define _AED_RUN_H_

#include "editor.h"

/*
 * CTRL+R: build the file on screen and run it, through hub.
 *
 * AED and the tools load at the same address, so AED cannot stay in memory
 * while one runs. CTRL+R saves the file, writes down which file it is and
 * where the cursor is in a hub block, asks hub to run the build, then the
 * program, then `aed -resume`, and leaves. hub does them in turn and starts
 * AED again, which opens the file where it was and, when the build failed,
 * goes to the first error.
 *
 *   .c           acc <file> -o <name>.bin -errors /aed.err, then <name>
 *   .s, .asm     zap <file> <name>.bin -c -e /aed.err, then <name>
 *   .bas         bbcbasic <file>, which runs it; *BYE comes back
 *
 * One file only. A directory with a project.ini is a project, and projects
 * are ade's: CTRL+R says so rather than building one file of it. Without hub
 * running -- and hub needs MOS 3.0.2 -- CTRL+R says that it needs hub, and
 * does nothing else.
 */

// The resume argument hub starts AED with when a run is over.
#define AED_RESUME_ARG "-resume"

// Builds and runs the file on screen. CTRL+R.
void aed_cmd_run(editor* ed);

// The file to open when AED was started with AED_RESUME_ARG, or NULL when
// there is no run to come back from.
const char* aed_resume_file(void);

// Comes back from a run: puts the cursor where it was, or on the build's
// first error, and says what happened. Call after ed_init, which opened the
// file aed_resume_file named.
void aed_resume(editor* ed);

#endif  // _AED_RUN_H_
