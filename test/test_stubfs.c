/*
 * Tests for the stubbed filesystem itself.
 *
 * Unusual, and worth the exception. The stubs used to model one buffer for
 * whatever was being written and one pointer to whatever was being read, which
 * is enough for an editor that opens a file, reads it whole and writes it back.
 * Paging is not that: it keeps two scratch files beside the document, reads and
 * writes at offsets in both, and the whole design turns on text being pushed
 * and popped at their ends.
 *
 * So the stub grew into a small filesystem, and everything step 2 proves about
 * paging it will prove against this. A test double nobody has checked is worse
 * than none -- it agrees with whatever the code does.
 *
 * See .internal/docs/PAGING.md.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-54s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static void check_bytes(const char* name, const char* got, int gotsz,
                        const char* want, int wantsz) {
    if (gotsz == wantsz && (gotsz == 0 || memcmp(got, want, (size_t) gotsz) == 0)) {
        fprintf(stderr, "PASS  %-54s %d bytes\n", name, gotsz);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d bytes, want %d\n", name, gotsz, wantsz);
        failures++;
    }
}

/* Writes `data` to `name`, truncating anything already there. */
static int put_file(const char* name, const char* data, int len) {
    const uint8_t fh = mos_fopen(name, FA_WRITE | FA_CREATE_ALWAYS);
    if (fh == 0) {
        return 0;
    }
    const unsigned wrote = mos_fwrite(fh, (char*) data, (unsigned) len);
    mos_fclose(fh);

    return wrote == (unsigned) len;
}

int main(void) {
    stub_discard_output();
    static char buf[256];

    /* --- a file written is a file readable --- */
    {
        stub_file_reset();
        check("a file can be written", put_file("/a.txt", "hello", 5), 1);
        const uint8_t fh = mos_fopen("/a.txt", FA_READ);
        check("  and opened again", fh != 0, 1);
        const unsigned n = mos_fread(fh, buf, sizeof(buf));
        mos_fclose(fh);
        check_bytes("  holding what was written", buf, (int) n, "hello", 5);
    }

    /* --- two files do not become one --- */
    {
        stub_file_reset();
        put_file("/one.txt", "AAAA", 4);
        put_file("/two.txt", "BBBBBBBB", 8);

        uint8_t fh = mos_fopen("/one.txt", FA_READ);
        unsigned n = mos_fread(fh, buf, sizeof(buf));
        mos_fclose(fh);
        check_bytes("the first file is its own", buf, (int) n, "AAAA", 4);

        fh = mos_fopen("/two.txt", FA_READ);
        n = mos_fread(fh, buf, sizeof(buf));
        mos_fclose(fh);
        check_bytes("  and the second is its own", buf, (int) n, "BBBBBBBB", 8);
    }

    /* --- both open at once, each with its own position --- */
    {
        stub_file_reset();
        put_file("/head.tmp", "0123456789", 10);
        put_file("/tail.tmp", "abcdefghij", 10);

        const uint8_t h = mos_fopen("/head.tmp", FA_READ);
        const uint8_t t = mos_fopen("/tail.tmp", FA_READ);
        check("two handles at once", h != 0 && t != 0 && h != t, 1);

        mos_fread(h, buf, 3);
        check_bytes("  the first reads its first three", buf, 3, "012", 3);
        mos_fread(t, buf, 3);
        check_bytes("  the second reads its own", buf, 3, "abc", 3);
        mos_fread(h, buf, 3);
        check_bytes("  and the first carries on where it was", buf, 3, "345", 3);
        mos_fclose(h);
        mos_fclose(t);
    }

    /* --- seeking, which is the whole of what a slide needs --- */
    {
        stub_file_reset();
        put_file("/s.tmp", "0123456789", 10);

        uint8_t fh = mos_fopen("/s.tmp", FA_READ);
        check("a seek is accepted", mos_flseek(fh, 4) == 0, 1);
        unsigned n = mos_fread(fh, buf, 3);
        check_bytes("  and the read starts there", buf, (int) n, "456", 3);
        mos_fclose(fh);

        /* Writing at an offset changes those bytes and no others, which is how
         * the tail gets text pushed back into it. */
        fh = mos_fopen("/s.tmp", FA_READ | FA_WRITE);
        mos_flseek(fh, 2);
        mos_fwrite(fh, "XY", 2);
        mos_fclose(fh);

        int len = 0;
        const char* all = stub_file_content("/s.tmp", &len);
        check_bytes("a write at an offset replaces only those bytes",
                    all, len, "01XY456789", 10);
    }

    /* --- a write past the end leaves a hole, and the file grows --- */
    {
        stub_file_reset();
        put_file("/h.tmp", "AB", 2);
        const uint8_t fh = mos_fopen("/h.tmp", FA_READ | FA_WRITE);
        mos_flseek(fh, 5);
        mos_fwrite(fh, "Z", 1);
        mos_fclose(fh);

        int len = 0;
        const char* all = stub_file_content("/h.tmp", &len);
        check("a write past the end grows the file", len, 6);
        check("  with the gap zeroed", all != NULL && all[2] == 0 && all[4] == 0, 1);
        check("  and the byte where it was put", all != NULL && all[5] == 'Z', 1);
    }

    /* --- opening to create truncates; appending does not --- */
    {
        stub_file_reset();
        put_file("/t.tmp", "original", 8);
        put_file("/t.tmp", "new", 3);
        int len = 0;
        const char* all = stub_file_content("/t.tmp", &len);
        check_bytes("creating again truncates", all, len, "new", 3);

        const uint8_t fh = mos_fopen("/t.tmp", FA_WRITE | FA_OPEN_APPEND);
        mos_fwrite(fh, "er", 2);
        mos_fclose(fh);
        all = stub_file_content("/t.tmp", &len);
        check_bytes("  and appending starts at the end", all, len, "newer", 5);
    }

    /* --- deleting and renaming, which the scratch files' lifecycle needs --- */
    {
        stub_file_reset();
        put_file("/gone.tmp", "x", 1);
        check("a written file exists", stub_file_exists("/gone.tmp"), 1);
        mos_del("/gone.tmp");
        check("  and after a delete it does not", stub_file_exists("/gone.tmp"), 0);
        check("  so opening it to read fails", mos_fopen("/gone.tmp", FA_READ), 0);

        put_file("/from.tmp", "carried", 7);
        check("a rename reports success", mos_ren("/from.tmp", "/to.tmp") == 0, 1);
        check("  the old name is gone", stub_file_exists("/from.tmp"), 0);
        int len = 0;
        const char* all = stub_file_content("/to.tmp", &len);
        check_bytes("  and the new one has the bytes", all, len, "carried", 7);
        check("renaming what is not there fails",
              mos_ren("/nothing.tmp", "/x.tmp") == 0, 0);
    }

    /* --- the fallback content, which is how most tests set a document up --- */
    {
        stub_file_reset();
        static const char doc[] = "a document";
        stub_file_set_content(doc, (int) sizeof(doc) - 1);

        uint8_t fh = mos_fopen("/anything.txt", FA_READ);
        check("an unknown name serves the fallback", fh != 0, 1);
        unsigned n = mos_fread(fh, buf, sizeof(buf));
        check_bytes("  which is the content", buf, (int) n, doc, (int) sizeof(doc) - 1);
        mos_fclose(fh);

        /* And once opened it is a file like any other: written to, it keeps
         * what was written rather than reverting to the fallback. */
        put_file("/anything.txt", "replaced", 8);
        fh = mos_fopen("/anything.txt", FA_READ);
        n = mos_fread(fh, buf, sizeof(buf));
        mos_fclose(fh);
        check_bytes("  and a write to it sticks", buf, (int) n, "replaced", 8);
    }

    /* --- with no fallback, a name that was never written is not there --- */
    {
        stub_file_reset();
        check("nothing to read means the open fails",
              mos_fopen("/never.tmp", FA_READ), 0);
        const uint8_t fh = mos_fopen("/never.tmp", FA_WRITE | FA_CREATE_ALWAYS);
        check("  but asking to write creates it", fh != 0, 1);
        mos_fclose(fh);
        check("  and then it is there", stub_file_exists("/never.tmp"), 1);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
