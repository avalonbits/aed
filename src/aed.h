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

#ifndef _AED_H_
#define _AED_H_

#include "aed_config.h"
#include "editor.h"
#include "user_input.h"

/*
 * AED itself, as distinct from the editing machinery it is built on: where
 * its files are, its keys, its commands for help and settings, and the help,
 * banner and settings screens those show.
 */

// AED's files: its settings, and where its grammars, themes and fonts live.
// ed_init hands this to the core with app_set.
extern const app_context AED_APP;

// AED's keys, which ed_init gives the editor.
extern const keymap AED_KEYS;

// Shows the settings, lets them be changed, and writes the file when any were.
void aed_cmd_settings(editor* ed);

// Shows the command list, then puts the document back.
void aed_cmd_help(editor* ed);

// Draws the command list over the text area and waits. Pages when the list is
// longer than the area, which it is on a 16-row font. Any key that is not a
// paging key closes it.
//
// It only draws: putting the document back is the caller's job, because the
// view cannot -- it has no access to the document. aed_cmd_help does it the
// same way aed_cmd_settings does.
void aed_help(user_input* ui, screen* scr);

// The startup banner, centred in the text area, for a session started with no
// file. Says what this is and where the commands are, and is wiped by the first
// keystroke rather than lingering behind the text.
void aed_banner(user_input* ui, screen* scr);

// The settings, editable. Draws over the text area; the caller puts the
// document back, as it does after the help. Returns YES_OPT when something was
// changed and is worth writing to the file.
//
// `cfg` comes in holding what AED is currently using and goes out holding the
// changes -- and only the changes: everything else stays unset, so writing it
// back with cfg_update cannot invent a setting the reader never asked for.
RESPONSE aed_settings(user_input* ui, screen* scr, config* cfg);

#endif  // _AED_H_
