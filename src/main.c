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

#include "editor.h"
#include "editor.h"
#include "screen.h"

#include <stdio.h>

int main(int argc, char** argv) {
    editor ed;

    const char* fname = NULL;
    if (argc > 1) {
        fname = argv[1];
    }
    if (!ed_init(&ed, 256, fname)) {
        return 1;
    }
    ed_run(&ed);

    ed_destroy(&ed);
    return 0;
}
