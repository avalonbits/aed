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

#include "aed_run.h"

#include <stdbool.h>
#include <string.h>

#include <agon/mos.h>

#include <hub/hub.h>

#include "aed_config.h"
#include "app.h"
#include "cmd_ops.h"
#include "user_input.h"

// Where the tools write their errors: `file:line:column: error: text`, one a
// line, and a result of 100. Deleted before each build, so an old one is
// never taken for the new build's.
#define ERR_FILE "/aed.err"

// A directory with this in it is a project, and projects are ade's.
#define PROJECT_FILE "project.ini"

// What the build makes, the file's name without its extension, is also how
// the program is run. Kept short enough for the message that names it.
#define PROGRAM_MAX 32

// What survives the run, in hub's block "AED ": the file, where the cursor
// was in it, and what was run.
typedef struct {
    char magic[4];                      // MAGIC once written
    char kind;                          // KIND_C, KIND_ASM or KIND_BAS
    char program[PROGRAM_MAX];          // the program's name, for the message
    int line;
    int x;                              // from 0
    char file[TB_FNAME_MAX];
} session;

// Changed whenever the session's layout does, so an older AED's is not
// misread by a newer one.
#define MAGIC "AED1"

enum {
    KIND_NONE = 0,
    KIND_C,
    KIND_ASM,
    KIND_BAS,
};

static session* block(void) {
    return (session*) hub_block("AED ", sizeof(session));
}

// The message row. ui_message keeps what it shows to the row.
static void say(editor* ed, const char* msg) {
    ui_message(&ed->ui_, &ed->scr_, (char*) msg);
}

// Appends `s` to `out` at `at`, short of `max` with room for the NUL; -1 when
// it does not fit. Built by hand: snprintf would link a printf for this alone.
static int put(char* out, int at, int max, const char* s) {
    if (at < 0) {
        return -1;
    }
    const int n = (int) strlen(s);
    if (at + n >= max) {
        return -1;
    }
    memcpy(out + at, s, (size_t) n);
    out[at + n] = 0;

    return at + n;
}

// Which tool the file is for, from its extension, with the name before the
// extension -- path and all -- in `base`.
static char kind_of(const char* name, char* base, int max) {
    const char* dot = strrchr(name, '.');
    const char* slash = strrchr(name, '/');
    if (dot == NULL || (slash != NULL && dot < slash) || dot == name) {
        return KIND_NONE;
    }
    const int n = (int) (dot - name);
    if (n >= max) {
        return KIND_NONE;
    }
    memcpy(base, name, (size_t) n);
    base[n] = 0;

    char ext[5] = { 0 };
    int i = 0;
    for (; i < 4 && dot[1 + i] != 0; i++) {
        const char c = dot[1 + i];
        ext[i] = (char) (c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    if (dot[1 + i] != 0) {
        return KIND_NONE;                   // longer than any of these
    }
    if (strcmp(ext, "c") == 0) {
        return KIND_C;
    }
    if (strcmp(ext, "s") == 0 || strcmp(ext, "asm") == 0) {
        return KIND_ASM;
    }
    if (strcmp(ext, "bas") == 0) {
        return KIND_BAS;
    }

    return KIND_NONE;
}

// The program's name for the message on coming back: the base without its
// directory, cut to fit.
static void program_name(const char* base, char* out) {
    const char* slash = strrchr(base, '/');
    const char* from = slash != NULL ? slash + 1 : base;
    int n = (int) strlen(from);
    if (n > PROGRAM_MAX - 1) {
        n = PROGRAM_MAX - 1;
    }
    memcpy(out, from, (size_t) n);
    out[n] = 0;
}

// A [run] command with its placeholders filled in, into `out`: %f the file,
// %b the file without its extension, %e the error file, %% a %. A % before
// anything else is kept as it is. -1 when the result does not fit.
static int expand(const char* tmpl, const char* name, const char* base,
                  char* out, int max) {
    int at = 0;
    out[0] = 0;
    for (const char* p = tmpl; *p != 0 && at >= 0; p++) {
        if (p[0] == '%' && p[1] != 0) {
            const char* with = NULL;
            switch (p[1]) {
                case 'f': with = name; break;
                case 'b': with = base; break;
                case 'e': with = ERR_FILE; break;
                case '%': with = "%"; break;
                default: break;
            }
            if (with != NULL) {
                at = put(out, at, max, with);
                p++;
                continue;
            }
        }
        const char one[2] = { *p, 0 };
        at = put(out, at, max, one);
    }

    return at;
}

// The [run] commands, read when CTRL+R is pressed: the settings file can have
// been edited since AED started, and it is short.
static aed_run_config run_cfg;

// The command for `kind`, the file's or the default when it sets none.
static const char* command_for(char kind) {
    const char* set = kind == KIND_C ? run_cfg.c
                    : kind == KIND_ASM ? run_cfg.asm_ : run_cfg.bas;
    if (set[0] != 0) {
        return set;
    }

    return kind == KIND_C ? AED_RUN_C : kind == KIND_ASM ? AED_RUN_ASM : AED_RUN_BAS;
}

void aed_cmd_run(editor* ed) {
    // Before anything is saved or asked: without hub there is nothing CTRL+R
    // can do, and saying so is all it should.
    if (!hub_present()) {
        say(ed, "CTRL+R needs hub to be running");
        return;
    }
    static FILINFO info;
    if (ffs_stat(&info, PROJECT_FILE) == FR_OK) {
        say(ed, "This is a project: build it with ade");
        return;
    }

    // A document with no name yet is given one, which is the only way it can
    // be saved for a tool to read.
    text_buffer* tb = &ed->doc_->buf_;
    if (!tb_valid_file(tb)) {
        cmd_save_as(ed);
        if (!tb_valid_file(tb)) {
            cmd_restore_after_modal(ed, false);
            return;
        }
    }

    const char* name = tb_fname(tb);
    static char base[TB_FNAME_MAX];
    const char kind = kind_of(name, base, TB_FNAME_MAX);
    if (kind == KIND_NONE) {
        say(ed, "CTRL+R builds and runs .c, .s, .asm and .bas files");
        return;
    }

    // The commands, as hub runs them from the prompt: from [run] in the
    // settings file, or the defaults.
    cfg_defaults(&AED_RUN_CONFIG, &run_cfg);
    cfg_load(&AED_RUN_CONFIG, &run_cfg, app_get()->cfg_path);
    static char build[HUB_CMD_MAX + 1];
    static char run[HUB_CMD_MAX + 1];
    const int max = HUB_CMD_MAX + 1;
    int b = 0;
    int r = 0;
    if (kind == KIND_BAS) {
        r = expand(command_for(kind), name, base, run, max);
    } else {
        b = expand(command_for(kind), name, base, build, max);
        r = put(run, 0, max, base);
    }
    if (b < 0 || r < 0) {
        say(ed, "The command is too long for hub to run");
        return;
    }

    // The tool reads the file, not the editor.
    if (!cmd_save(ed)) {
        say(ed, "The file could not be saved");
        return;
    }

    session* s = block();
    if (s == NULL) {
        say(ed, "hub has no room to keep AED's place");
        return;
    }
    memcpy(s->magic, MAGIC, 4);
    s->kind = kind;
    program_name(base, s->program);
    s->line = tb_ypos(tb);
    s->x = tb_xpos(tb) - 1;
    memcpy(s->file, name, strlen(name) + 1);

    mos_del(ERR_FILE);
    bool queued = hub_enter("AED ") == HUB_OK;
    if (queued && kind != KIND_BAS) {
        queued = hub_push(build, HUB_STOP_ON_ERROR) == HUB_OK;
    }
    // On the screen as hub's prompt has it, and held for a key afterwards,
    // so what the program printed can be read before AED paints over it.
    if (queued) {
        queued = hub_push(run, HUB_USER_PROGRAM | HUB_PAUSE_AFTER) == HUB_OK;
    }
    if (!queued || hub_return_to("aed " AED_RESUME_ARG) != HUB_OK) {
        memset(s->magic, 0, 4);
        say(ed, "hub would not take the run");
        return;
    }
    ed->leaving_ = true;
}

const char* aed_resume_file(void) {
    if (!hub_present()) {
        return NULL;
    }
    const session* s = block();
    if (s == NULL || memcmp(s->magic, MAGIC, 4) != 0 || s->file[0] == 0) {
        return NULL;
    }

    return s->file;
}

// The first line of the error file, NUL-terminated, or NULL without one.
static char errs[256];

static const char* first_error(void) {
    const char fh = mos_fopen(ERR_FILE, FA_READ);
    if (fh == 0) {
        return NULL;
    }
    const int n = (int) mos_fread(fh, errs, sizeof(errs) - 1);
    mos_fclose(fh);
    if (n <= 0) {
        return NULL;
    }
    int end = 0;
    while (end < n && errs[end] != '\r' && errs[end] != '\n') {
        end++;
    }
    errs[end] = 0;

    return end > 0 ? errs : NULL;
}

// The place an error names: its file's length, and the line and column after
// it. The file may hold a drive's colon, so the numbers start at the first ':'
// with a digit after it. False when it names no line.
static bool error_place(const char* e, int* flen, int* line, int* col) {
    const char* c1 = NULL;
    for (const char* q = e; *q != 0; q++) {
        if (q[0] == ':' && q[1] >= '0' && q[1] <= '9') {
            c1 = q;
            break;
        }
    }
    if (c1 == NULL || c1 == e) {
        return false;
    }
    *flen = (int) (c1 - e);
    *line = 0;
    *col = 0;
    const char* q = c1 + 1;
    while (*q >= '0' && *q <= '9') {
        *line = *line * 10 + (*q++ - '0');
    }
    if (*q == ':') {
        q++;
        while (*q >= '0' && *q <= '9') {
            *col = *col * 10 + (*q++ - '0');
        }
    }

    return *line > 0;
}

// What an error says past its `file:line:column: `: the cursor is on the
// place, and the row is too short to say it twice.
static const char* error_text(const char* e) {
    const char* q = e;
    while (*q != 0 && !(q[0] == ':' && q[1] >= '0' && q[1] <= '9')) {
        q++;
    }
    for (int field = 0; field < 2 && *q == ':'; field++) {
        q++;
        while (*q >= '0' && *q <= '9') {
            q++;
        }
    }
    if (*q != ':') {
        return e;
    }
    q++;
    while (*q == ' ') {
        q++;
    }

    return *q != 0 ? q : e;
}

void aed_resume(editor* ed) {
    // First, as hub asks: until it has been found, every other call fails --
    // and a failed hub_failed_job reads as job 0, the build, having failed.
    if (!hub_present()) {
        return;
    }
    session* s = block();
    if (s == NULL || memcmp(s->magic, MAGIC, 4) != 0) {
        return;
    }
    memset(s->magic, 0, 4);             // used: a later start is not a resume

    int line = s->line;
    int x = s->x;
    const char* say_this = NULL;

    // The build is the first job, and only a build stops the frame: a .bas
    // file is run without one.
    const bool failed = s->kind != KIND_BAS && hub_failed_job() == 0;
    if (failed) {
        const char* e = first_error();
        int flen = 0;
        int eline = 0;
        int ecol = 0;
        // Only an error in this file moves the cursor: one in a header it
        // includes names somewhere this editor does not have open.
        if (e != NULL && error_place(e, &flen, &eline, &ecol)
                && flen == (int) strlen(s->file)
                && memcmp(e, s->file, (size_t) flen) == 0) {
            line = eline;
            x = ecol > 0 ? ecol - 1 : 0;
            say_this = error_text(e);
        } else if (e != NULL) {
            say_this = e;
        } else {
            say_this = "The build failed";
        }
    } else if (s->kind != KIND_BAS && hub_last_result() != 0) {
        // The program ran and said something was wrong.
        static char msg[PROGRAM_MAX + 24];
        int at = ui_put_at(msg, 0, (int) sizeof(msg) - 1, s->program);
        at = ui_put_at(msg, at, (int) sizeof(msg) - 1, " returned ");
        at = ui_put_num(msg, at, (int) sizeof(msg) - 1, hub_last_result());
        msg[at] = 0;
        say_this = msg;
    }

    ed_doc_place(ed, ed->doc_, ed->scr_.v_, line, x);
    cmd_restore_after_modal(ed, false);
    if (say_this != NULL) {
        say(ed, say_this);
        cmd_restore_after_modal(ed, false);
    }
}
