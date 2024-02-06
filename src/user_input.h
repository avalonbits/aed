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

#ifndef _USER_INPUT_H_
#define _USER_INPUT_H_

#include "char_buffer.h"
#include "screen.h"

typedef enum _response {
    CANCEL_OPT = 0,
    YES_OPT = 1,
    NO_OPT = 2,
} RESPONSE;


typedef struct _user_input {
    char_buffer cb_;
    char ypos_;
    char cols_;
} user_input;

user_input* ui_init(user_input* ui, int size, char ypos, char cols);
void ui_destroy(user_input* ui);

RESPONSE ui_goto(user_input* ui, screen* scr, int* line);
RESPONSE ui_color_picker(user_input* ui, screen* scr);
RESPONSE ui_dialog(user_input* ui, screen* scr, char* msg);
RESPONSE ui_text(
    user_input* ui,
    screen* scr,
    char* title,
    char* prefill,
    char** buf,
    int* sz);

#endif  // _USER_INPUT_H_
