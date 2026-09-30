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

#ifndef _AED_CONFIG_H_
#define _AED_CONFIG_H_

#include <stdbool.h>

#include "config.h"
#include "settings.h"

/*
 * AED's settings: the editor's, from settings.h, in /config/aed.ini, with a
 * fresh file naming AED. The file format and the reading and rewriting of it
 * are the engine's, in config.h; the settings and the screen that changes
 * them are the editor library's, in settings.h; this is the schema AED hands
 * both.
 */

// The name AED's code has always used for its settings.
typedef ed_settings config;

extern const cfg_schema AED_CONFIG;

#endif  // _AED_CONFIG_H_
