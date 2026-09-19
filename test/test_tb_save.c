/*
 * Host tests for tb_save().
 *
 * Saving used to live in the controller (cmd_ops.c's save_file), which meant the
 * model could load a file it had no way to write and `fname_` had two owners.
 * These tests build a document through the model's own API and assert on the
 * bytes handed to mos_fwrite, so they cover the write path itself -- not just
 * that some function was called.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "text_buffer.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-46s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-46s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static void check_bytes(const char* name, const char* got, int gotsz,
                        const char* want) {
    const int wantsz = (int) strlen(want);
    if (gotsz == wantsz && memcmp(got, want, wantsz) == 0) {
        fprintf(stderr, "PASS  %-46s %d bytes\n", name, gotsz);
    } else {
        fprintf(stderr, "FAIL  %-46s got %d bytes %.*s, want %d bytes %s\n",
                name, gotsz, gotsz, got, wantsz, want);
        failures++;
    }
}

static void put_str(text_buffer* tb, const char* s) {
    for (; *s; s++) {
        tb_put(tb, *s);
    }
}

int main(void) {
    stub_discard_output();

    text_buffer tb;

    /* A model with no backing file cannot be saved. */
    stub_file_reset();
    if (!tb_init(&tb, 8, NULL)) {
        fprintf(stderr, "tb_init failed\n");

        return 2;
    }
    check("no filename -> tb_save refuses", tb_save(&tb), 0);
    check("no filename -> nothing opened", stub_file_opens(), 0);

    /* Naming it makes it saveable, and the model owns the name. */
    tb_set_fname(&tb, "test.txt", 8);
    check("tb_set_fname -> tb_valid_file", tb_valid_file(&tb) ? 1 : 0, 1);
    check_bytes("tb_fname round-trips", tb_fname(&tb),
                (int) strlen(tb_fname(&tb)), "test.txt");

    /* Cursor at the end: the whole document is in the prefix. */
    put_str(&tb, "hello");
    check("editing marks the buffer dirty", tb_changed(&tb) ? 1 : 0, 1);
    stub_file_reset();
    check("tb_save succeeds", tb_save(&tb) ? 1 : 0, 1);
    check_bytes("writes the document", stub_file_bytes(), stub_file_size(), "hello");
    check("tb_save clears the dirty flag", tb_changed(&tb) ? 1 : 0, 0);
    check("file was closed", stub_file_closes(), 1);

    /* Cursor in the middle: prefix and suffix must be written in order, with
     * the gap between them skipped. This is the part that would silently
     * corrupt a save if the two segments were mishandled. */
    tb_home(&tb);
    tb_next(&tb);
    tb_next(&tb);
    stub_file_reset();
    check("tb_save with a mid-document cursor", tb_save(&tb) ? 1 : 0, 1);
    check_bytes("prefix+suffix rejoin in order", stub_file_bytes(),
                stub_file_size(), "hello");

    /* Multi-line, cursor mid-buffer: CRLF must survive the split. */
    tb_end(&tb);
    tb_newline(&tb);
    put_str(&tb, "world");
    tb_home(&tb);
    stub_file_reset();
    check("multi-line save", tb_save(&tb) ? 1 : 0, 1);
    check_bytes("newline preserved across the gap", stub_file_bytes(),
                stub_file_size(), "hello\r\nworld");

    /* A failed open must not report success, and must not leave the buffer
     * looking saved when it is not. */
    put_str(&tb, "z");
    stub_file_reset();
    stub_file_fail_open(1);
    check("open failure -> tb_save fails", tb_save(&tb) ? 1 : 0, 0);
    check("open failure -> nothing written", stub_file_size(), 0);
    check("open failure -> still dirty", tb_changed(&tb) ? 1 : 0, 1);

    /* A card that fills up mid-write is the other way a save fails, and the
     * one that reports success if the sink ignores what mos_fwrite returned.
     * The document is long enough to need more than one write, so the short
     * one lands partway through. */
    stub_file_reset();
    stub_file_fail_open(0);
    tb_end(&tb);
    for (int i = 0; i < 40; i++) {
        put_str(&tb, "padding");
    }
    stub_file_short_write(8);
    check("a card that fills up -> tb_save fails", tb_save(&tb) ? 1 : 0, 0);
    check("  and leaves the buffer dirty", tb_changed(&tb) ? 1 : 0, 1);

    /* And again with the cursor at the top, so the document is all suffix:
     * the second of the two writes is the one that comes up short. */
    tb_pos top = { 1, 0 };
    tb_seek(&tb, top);
    stub_file_reset();
    stub_file_short_write(8);
    check("  the same when the suffix is what fills it",
          tb_save(&tb) ? 1 : 0, 0);
    check("    and it is still dirty", tb_changed(&tb) ? 1 : 0, 1);
    stub_file_short_write(-1);

    tb_destroy(&tb);

    /*
     * The document on the card must survive a save that fails.
     *
     * The in-memory save used to open the document itself with
     * FA_CREATE_ALWAYS, which empties it before it has a replacement to put
     * back. Every failure above therefore left the reader with a file that had
     * been emptied and an editor holding the only copy -- and closing the
     * editor at that point lost the file. Checked of the card rather than of
     * the editor, because the question is what is on disk afterwards.
     */
    {
        static const char KEEP[] = "the file that was already there\r\n";
        stub_file_reset();
        stub_file_add("/keep.txt", KEEP, (int) sizeof(KEEP) - 1);

        static text_buffer t;
        tb_init(&t, 8, NULL);
        tb_set_fname(&t, "/keep.txt", 9);
        for (int i = 0; i < 40; i++) {
            put_str(&t, "replacement");
        }

        stub_file_short_write(8);       /* a card that fills up partway */
        check("a save that fails is reported", tb_save(&t) ? 1 : 0, 0);
        stub_file_short_write(-1);

        static char back[128];
        const char rh = mos_fopen("/keep.txt", FA_READ);
        const int got = rh != 0 ? (int) mos_fread(rh, back, sizeof(back)) : -1;
        if (rh != 0) {
            mos_fclose(rh);
        }
        check("  and the file still holds what it held",
              got, (int) sizeof(KEEP) - 1);
        check("    byte for byte",
              got == (int) sizeof(KEEP) - 1
                  && memcmp(back, KEEP, (size_t) got) == 0 ? 1 : 0, 1);

        /* The scratch file it wrote through is cleaned up, so a failed save
         * does not litter the card with half-written leftovers. */
        const char sh = mos_fopen("/keep.txt.aeds", FA_READ);
        if (sh != 0) {
            mos_fclose(sh);
        }
        check("  no scratch file left behind", sh != 0 ? 1 : 0, 0);

        /* A save that works still replaces the file. */
        stub_file_reset();
        stub_file_add("/keep.txt", KEEP, (int) sizeof(KEEP) - 1);
        check("a save that works replaces it", tb_save(&t) ? 1 : 0, 1);
        const char oh = mos_fopen("/keep.txt", FA_READ);
        static char after[1024];
        const int asz = oh != 0 ? (int) mos_fread(oh, after, sizeof(after)) : -1;
        if (oh != 0) {
            mos_fclose(oh);
        }
        check("  the document is what is on the card", asz, 40 * 11);

        check("    and the scratch file is gone",
              mos_fopen("/keep.txt.aeds", FA_READ) != 0 ? 1 : 0, 0);

        /*
         * Saving over a document that is already there.
         *
         * The scratch file cannot simply be renamed into place: FatFS answers
         * FR_EXIST for a target that exists and so does MOS, so the document
         * has to be removed first. The stub modelled a rename as an overwrite
         * for a while, and every test here passed against a save that could
         * not work on an Agon -- a reader on real hardware could not save at
         * all, and was told so on the way out.
         */
        stub_file_reset();
        stub_file_add("/keep.txt", KEEP, (int) sizeof(KEEP) - 1);
        static text_buffer over;
        tb_init(&over, 8, NULL);
        tb_set_fname(&over, "/keep.txt", 9);
        put_str(&over, "the replacement");
        check("a save over a document that exists", tb_save(&over) ? 1 : 0, 1);
        check("  and the buffer is clean afterwards",
              tb_changed(&over) ? 1 : 0, 0);
        {
            static char back[64];
            const char rh = mos_fopen("/keep.txt", FA_READ);
            const int got = rh != 0
                          ? (int) mos_fread(rh, back, sizeof(back)) : -1;
            if (rh != 0) {
                mos_fclose(rh);
            }
            check("    the card holds the new document", got, 15);
            check("      and not the old one",
                  got == 15 && memcmp(back, "the replacement", 15) == 0
                      ? 1 : 0, 1);
        }
        /* Twice over, because the second save is the one that has both a
         * document and a scratch name to contend with. */
        put_str(&over, "!");
        check("  and again", tb_save(&over) ? 1 : 0, 1);
        check("    with no scratch file left",
              mos_fopen("/keep.txt.aeds", FA_READ) != 0 ? 1 : 0, 0);
        tb_destroy(&over);

        /*
         * A rename that fails leaves the scratch file alone.
         *
         * Between removing the document and renaming the scratch file over it,
         * the only complete copy of the reader's work is that scratch file.
         * Tidying it away on the error path is the one thing that would turn a
         * failed save into a lost document, so it stays -- under a name beside
         * the document's, where it can be found.
         */
        stub_file_reset();
        stub_file_add("/keep.txt", KEEP, (int) sizeof(KEEP) - 1);
        static text_buffer stuck;
        tb_init(&stuck, 8, NULL);
        tb_set_fname(&stuck, "/keep.txt", 9);
        put_str(&stuck, "work that must survive");
        stub_file_fail_ren(1);
        check("a save whose rename fails is reported",
              tb_save(&stuck) ? 1 : 0, 0);
        check("  and the buffer stays dirty", tb_changed(&stuck) ? 1 : 0, 1);
        {
            static char back[64];
            const char rh = mos_fopen("/keep.txt.aeds", FA_READ);
            const int got = rh != 0
                          ? (int) mos_fread(rh, back, sizeof(back)) : -1;
            if (rh != 0) {
                mos_fclose(rh);
            }
            check("    the work is on the card under the scratch name", got, 22);
            check("      whole", got == 22
                  && memcmp(back, "work that must survive", 22) == 0 ? 1 : 0, 1);
        }
        tb_destroy(&stuck);
        tb_destroy(&t);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
