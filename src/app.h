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

#ifndef _APP_H_
#define _APP_H_

/*
 * Who the core is working for.
 *
 * The document model is shared by more than one program, and a few of the
 * things it does depend on which program that is: an unnamed document's
 * scratch files take the program's name, and the settings, grammars, themes
 * and fonts live where the program keeps them. Those are this struct, filled
 * in by the program and handed over once at startup with app_set, before any
 * document opens. The core holds the pointer, so the struct must outlive
 * every document -- a static const is the usual shape.
 *
 * Nothing in the core names a program itself. A core call that needs the
 * context and finds none set fails, the way it would for a path that does
 * not fit, so a program that forgets app_set finds out at its first unnamed
 * document.
 */
typedef struct _app_context {
    // A plain name for the scratch files of a document that has none of its
    // own: <name>.aedh, <name>.aedt, <name>.scratch in the current directory.
    const char* name;

    // The settings file, and the name it had before, which cfg_migrate moves
    // across once. NULL for a program that never had another.
    const char* cfg_path;
    const char* cfg_old;

    // The directories grammars, themes and fonts are read from.
    const char* syntax_dir;
    const char* theme_dir;
    const char* font_dir;
} app_context;

void app_set(const app_context* app);

// The context app_set was given, or NULL before it has been called.
const app_context* app_get(void);

#endif  // _APP_H_
