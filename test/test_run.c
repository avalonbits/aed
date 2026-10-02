/*
 * CTRL+R: build and run the file on screen, through hub, and come back.
 *
 * What CTRL+R does is queue: the build, the program, and AED again with
 * -resume, then leave. So these read back what it queued from a stub hub, and
 * then play the coming back as hub would start it -- with the build failed or
 * not -- and look at where the cursor ends up and what is said.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>
#include <hub/hub.h>

#include "aed.h"
#include "aed_config.h"
#include "aed_run.h"
#include "hub_stub.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-54s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static void check_str(const char* name, const char* got, const char* want) {
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %-54s %s\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got \"%s\", want \"%s\"\n", name, got, want);
        failures++;
    }
}

static long mark;

static void cap_start(void) {
    fflush(stdout);
    mark = ftell(stdout);
}

static char got[65536];

static int cap_read(void) {
    fflush(stdout);
    FILE* r = fopen("/tmp/aed_run_capture", "rb");
    if (r == NULL) {
        return 0;
    }
    fseek(r, mark, SEEK_SET);
    const int n = (int) fread(got, 1, sizeof(got) - 1, r);
    fclose(r);
    got[n > 0 ? n : 0] = 0;

    return n;
}

static int said(int n, const char* want) {
    const int w = (int) strlen(want);
    for (int i = 0; i + w <= n; i++) {
        if (memcmp(got + i, want, (size_t) w) == 0) {
            return 1;
        }
    }

    return 0;
}

static editor ed;

static const char PROG_C[] = "int main(void) {\r\n    return 0;\r\n}\r\n";

/* A card with `file` on it, holding `text`, and the editor started on it. */
static void start(const char* file, const char* text) {
    stub_file_reset();
    stub_file_add(file, text, (int) strlen(text));
    ed_init(&ed, 8, file);
}

/* CTRL+R on what the editor has open; what it said comes back in `got`. */
static int run_it(void) {
    cap_start();
    aed_cmd_run(&ed);

    return cap_read();
}

int main(void) {
    if (freopen("/tmp/aed_run_capture", "w+", stdout) == NULL) {
        return 2;
    }
    stub_set_screen(80, 25);

    /* --- without hub, it says so and does nothing else --- */
    stub_hub_reset(false);
    start("prog.c", PROG_C);
    int n = run_it();
    check("without hub, CTRL+R says it needs hub", said(n, "needs hub to be running"), 1);
    check("  and queues nothing", stub_hub_jobs(), 0);
    check("  and stays", ed.leaving_, 0);
    ed_destroy(&ed);

    /* --- a project is ade's --- */
    stub_hub_reset(true);
    start("prog.c", PROG_C);
    stub_file_add("project.ini", "[project]\r\nname = demo\r\n", 25);
    n = run_it();
    check("in a project, CTRL+R points at ade", said(n, "build it with ade"), 1);
    check("  and queues nothing", stub_hub_jobs(), 0);
    ed_destroy(&ed);

    /* --- a file no tool builds --- */
    stub_hub_reset(true);
    start("notes.txt", "hello\r\n");
    n = run_it();
    check("a .txt file is not one CTRL+R runs", said(n, ".c, .s, .asm and .bas"), 1);
    check("  and queues nothing", stub_hub_jobs(), 0);
    ed_destroy(&ed);

    /* --- a .c file: acc, then the program, then AED again --- */
    stub_hub_reset(true);
    start("prog.c", PROG_C);
    tb_seek(&ed.doc_->buf_, (tb_pos) { .line = 2, .x = 4 });
    tb_settle(&ed.doc_->buf_);
    run_it();
    check("a .c file queues the build and the run", stub_hub_jobs(), 2);
    check_str("  the build is acc's", stub_hub_job(0),
              "acc prog.c -o prog.bin -errors /aed.err");
    check("  and stops the run when it fails", stub_hub_flags(0), HUB_STOP_ON_ERROR);
    check_str("  then the program", stub_hub_job(1), "prog");
    check("  on hub's screen, waiting for a key after",
          stub_hub_flags(1), HUB_USER_PROGRAM | HUB_PAUSE_AFTER);
    check_str("  then AED again", stub_hub_continuation(), "aed -resume");
    check("  and AED leaves for them to run", ed.leaving_, 1);
    ed_destroy(&ed);

    /* --- and coming back after it ran --- */
    stub_hub_ran(-1, 0);
    check_str("coming back opens the file it was in", aed_resume_file(), "prog.c");
    stub_file_reset();
    stub_file_add("prog.c", PROG_C, (int) strlen(PROG_C));
    ed_init(&ed, 8, aed_resume_file());
    cap_start();
    aed_resume(&ed);
    n = cap_read();
    check("  with the cursor on the line it was on", tb_ypos(&ed.doc_->buf_), 2);
    check("  at the column it was at", tb_xpos(&ed.doc_->buf_), 5);
    check("  saying nothing when it went well", said(n, "returned"), 0);
    check("  and only once: the next start is not a resume",
          aed_resume_file() == NULL ? 1 : 0, 1);
    ed_destroy(&ed);

    /* --- a build that failed comes back on the first error --- */
    stub_hub_reset(true);
    start("prog.c", PROG_C);
    run_it();
    ed_destroy(&ed);
    stub_hub_ran(0, 100);
    stub_file_reset();
    stub_file_add("prog.c", PROG_C, (int) strlen(PROG_C));
    static const char ERRS[] =
        "prog.c:2:12: error: expected ';' after return\r\n"
        "prog.c:3:1: error: something after it\r\n";
    stub_file_add("/aed.err", ERRS, (int) strlen(ERRS));
    ed_init(&ed, 8, aed_resume_file());
    cap_start();
    aed_resume(&ed);
    n = cap_read();
    check("a failed build comes back on its first error's line",
          tb_ypos(&ed.doc_->buf_), 2);
    check("  and column", tb_xpos(&ed.doc_->buf_), 12);
    check("  saying what it is", said(n, "expected ';' after return"), 1);
    check("  without the place, which the cursor shows", said(n, "prog.c:2:12"), 0);
    ed_destroy(&ed);

    /* --- an error in another file says so, and leaves the cursor --- */
    stub_hub_reset(true);
    start("prog.c", PROG_C);
    tb_seek(&ed.doc_->buf_, (tb_pos) { .line = 3, .x = 0 });
    tb_settle(&ed.doc_->buf_);
    run_it();
    ed_destroy(&ed);
    stub_hub_ran(0, 100);
    stub_file_reset();
    stub_file_add("prog.c", PROG_C, (int) strlen(PROG_C));
    static const char OTHER[] = "lib.h:7:3: error: unknown type\r\n";
    stub_file_add("/aed.err", OTHER, (int) strlen(OTHER));
    ed_init(&ed, 8, aed_resume_file());
    cap_start();
    aed_resume(&ed);
    n = cap_read();
    check("an error in another file is said in full", said(n, "lib.h:7:3"), 1);
    check("  and the cursor stays where it was", tb_ypos(&ed.doc_->buf_), 3);
    ed_destroy(&ed);

    /* --- coming back finds hub itself --- */
    /* Started by hub, AED has not asked for it yet when aed_resume runs on its
     * own: every hub call fails until it does, so the session would not be
     * found and the cursor would not go back. */
    stub_hub_reset(true);
    start("prog.c", PROG_C);
    tb_seek(&ed.doc_->buf_, (tb_pos) { .line = 3, .x = 0 });
    tb_settle(&ed.doc_->buf_);
    run_it();
    ed_destroy(&ed);
    stub_hub_ran(-1, 0);
    stub_file_reset();
    stub_file_add("prog.c", PROG_C, (int) strlen(PROG_C));
    ed_init(&ed, 8, "prog.c");
    aed_resume(&ed);
    check("aed_resume finds hub itself, and goes back to the line",
          tb_ypos(&ed.doc_->buf_), 3);
    ed_destroy(&ed);

    /* --- a program that returned something else --- */
    stub_hub_reset(true);
    start("prog.c", PROG_C);
    run_it();
    ed_destroy(&ed);
    stub_hub_ran(-1, 3);
    stub_file_reset();
    stub_file_add("prog.c", PROG_C, (int) strlen(PROG_C));
    ed_init(&ed, 8, aed_resume_file());
    cap_start();
    aed_resume(&ed);
    n = cap_read();
    check("a program that returned 3 says so", said(n, "prog returned 3"), 1);
    ed_destroy(&ed);

    /* --- assembly, with zap --- */
    stub_hub_reset(true);
    start("/src/boot.asm", "    ret\r\n");
    run_it();
    check_str("a .asm file is built by zap", stub_hub_job(0),
              "zap /src/boot.asm /src/boot.bin -c -e /aed.err");
    check_str("  and run by its path", stub_hub_job(1), "/src/boot");
    ed_destroy(&ed);
    stub_hub_reset(true);
    start("BOOT.S", "    ret\r\n");
    run_it();
    check_str("a .S file, in capitals, is zap's too", stub_hub_job(0),
              "zap BOOT.S BOOT.bin -c -e /aed.err");
    ed_destroy(&ed);

    /* --- BASIC: run, with nothing to build --- */
    stub_hub_reset(true);
    start("game.bas", "10 PRINT \"HI\"\r\n");
    run_it();
    check("a .bas file queues only the run", stub_hub_jobs(), 1);
    check_str("  by BBC BASIC", stub_hub_job(0), "bbcbasic game.bas");
    check("  on hub's screen, waiting for a key after",
          stub_hub_flags(0), HUB_USER_PROGRAM | HUB_PAUSE_AFTER);
    ed_destroy(&ed);
    /* Coming back from BASIC is never a failed build. */
    stub_hub_ran(-1, 0);
    stub_file_reset();
    stub_file_add("game.bas", "10 PRINT \"HI\"\r\n", 15);
    ed_init(&ed, 8, aed_resume_file());
    cap_start();
    aed_resume(&ed);
    n = cap_read();
    check("coming back from BASIC says nothing", said(n, "failed"), 0);
    ed_destroy(&ed);

    /* --- the file is saved first: the tool reads the card --- */
    stub_hub_reset(true);
    start("prog.c", PROG_C);
    tb_seek(&ed.doc_->buf_, (tb_pos) { .line = 1, .x = 0 });
    tb_settle(&ed.doc_->buf_);
    tb_put(&ed.doc_->buf_, 'x');
    run_it();
    {
        int len = 0;
        const char* saved = stub_file_content("prog.c", &len);
        check("an edited file is saved before the build",
              saved != NULL && len > 0 && saved[0] == 'x' ? 1 : 0, 1);
    }
    ed_destroy(&ed);

    /* --- the commands come from [run] in aed.ini --- */
    stub_hub_reset(true);
    stub_file_reset();
    stub_file_add("prog.c", PROG_C, (int) strlen(PROG_C));
    static const char RUN_INI[] =
        "[run]\r\n"
        "c = mycc %f -o %b.bin --errors=%e -O2\r\n"
        "asm = ez80asm %f %b.bin\r\n"
        "bas = bbcbasic24 %f\r\n";
    stub_file_add("/config/aed.ini", RUN_INI, (int) strlen(RUN_INI));
    ed_init(&ed, 8, "prog.c");
    run_it();
    check_str("[run] c is the build", stub_hub_job(0),
              "mycc prog.c -o prog.bin --errors=/aed.err -O2");
    check_str("  and %b is still what runs after", stub_hub_job(1), "prog");
    ed_destroy(&ed);

    stub_hub_reset(true);
    stub_file_reset();
    stub_file_add("game.bas", "10 END\r\n", 8);
    stub_file_add("/config/aed.ini", RUN_INI, (int) strlen(RUN_INI));
    ed_init(&ed, 8, "game.bas");
    run_it();
    check_str("[run] bas is the run", stub_hub_job(0), "bbcbasic24 game.bas");
    ed_destroy(&ed);

    /* %% is a %, and a % before anything else stays as it is. */
    stub_hub_reset(true);
    stub_file_reset();
    stub_file_add("boot.s", "    ret\r\n", 9);
    static const char PCT_INI[] = "[run]\r\nasm = as %b 100%% %x %\r\n";
    stub_file_add("/config/aed.ini", PCT_INI, (int) strlen(PCT_INI));
    ed_init(&ed, 8, "boot.s");
    run_it();
    check_str("%% gives a %, and %x and a last % are kept", stub_hub_job(0),
              "as boot 100% %x %");
    ed_destroy(&ed);

    /* An empty command is the default. */
    stub_hub_reset(true);
    stub_file_reset();
    stub_file_add("prog.c", PROG_C, (int) strlen(PROG_C));
    static const char EMPTY_INI[] = "[run]\r\nc =\r\n";
    stub_file_add("/config/aed.ini", EMPTY_INI, (int) strlen(EMPTY_INI));
    ed_init(&ed, 8, "prog.c");
    run_it();
    check_str("an empty c is acc, as by default", stub_hub_job(0),
              "acc prog.c -o prog.bin -errors /aed.err");
    ed_destroy(&ed);

    /* Longer than hub takes, once filled in: said, and nothing queued. */
    stub_hub_reset(true);
    stub_file_reset();
    stub_file_add("prog.c", PROG_C, (int) strlen(PROG_C));
    static const char LONG_INI[] =
        "[run]\r\nc = acc %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f\r\n";
    stub_file_add("/config/aed.ini", LONG_INI, (int) strlen(LONG_INI));
    ed_init(&ed, 8, "prog.c");
    n = run_it();
    check("a command too long for hub is said", said(n, "too long for hub"), 1);
    check("  and nothing is queued", stub_hub_jobs(), 0);
    ed_destroy(&ed);

    /* --- a fresh aed.ini carries [run], with the defaults --- */
    stub_hub_reset(true);
    stub_file_reset();
    stub_file_add("prog.c", PROG_C, (int) strlen(PROG_C));
    ed_init(&ed, 8, "prog.c");
    {
        int len = 0;
        const char* ini = stub_file_content("/config/aed.ini", &len);
        static aed_run_config rc;
        cfg_defaults(&AED_RUN_CONFIG, &rc);
        if (ini != NULL) {
            cfg_parse(&AED_RUN_CONFIG, &rc, ini, len);
        }
        check_str("a first run writes [run] c", rc.c, AED_RUN_C);
        check_str("  asm", rc.asm_, AED_RUN_ASM);
        check_str("  and bas", rc.bas, AED_RUN_BAS);
    }
    ed_destroy(&ed);

    /* --- and saving a setting from CTRL+E keeps it --- */
    stub_file_reset();
    static const char KEEP_INI[] =
        "[editor]\r\ntab = 4\r\n[run]\r\nc = mycc %f\r\n";
    stub_file_add("/config/aed.ini", KEEP_INI, (int) strlen(KEEP_INI));
    {
        config cfg;
        cfg_defaults(&AED_CONFIG, &cfg);
        cfg.tab_size = 8;
        cfg_update(&AED_CONFIG, &cfg, "/config/aed.ini");
        int len = 0;
        const char* ini = stub_file_content("/config/aed.ini", &len);
        static aed_run_config rc;
        cfg_defaults(&AED_RUN_CONFIG, &rc);
        if (ini != NULL) {
            cfg_parse(&AED_RUN_CONFIG, &rc, ini, len);
        }
        check_str("a settings change leaves [run] as it was", rc.c, "mycc %f");
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
