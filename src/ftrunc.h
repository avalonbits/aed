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

#ifndef _FTRUNC_H_
#define _FTRUNC_H_

#include <agon/mos.h>

/*
 * Truncates a file at its current position. MOS 2.3.0 and above; the supported
 * floor is 2.3.3.
 *
 * Use this and not agondev's ffs_ftruncate, whose stub pops IX without pushing
 * it and returns into hyperspace -- see src/ftrunc.asm for the disassembly and
 * test/probes/ftruncate.c for the measurement. Returns an FRESULT: 0 is FR_OK.
 *
 * Nothing calls it yet. It is here because isolating that fault took a day's
 * confusion -- the first conclusion was that MOS 2.3's truncate did not work
 * and the floor bump had bought nothing -- and because incremental save is the
 * next thing that wants it.
 */
uint8_t aed_ftruncate(FIL* fh);

#endif  // _FTRUNC_H_
