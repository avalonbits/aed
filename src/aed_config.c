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

#include "aed_config.h"

// A fresh aed.ini: the editor's file, naming AED, with the example font among
// AED's own.
static int render(const void* values, char* buf, int max) {
    return ed_settings_render(values, "AED", CFG_DIR "/aed", buf, max);
}

const cfg_schema AED_CONFIG = {
    ED_SETTINGS, ED_SETTINGS_COUNT, render,
};
