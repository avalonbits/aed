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

/*
 * Host tests for Find.
 *
 * The bug these exist for is the gap. char_buffer keeps the document in two
 * pieces split at wherever the cursor happens to be, so a search that scans it
 * as memory reads the gap's stale bytes, and one that scans the two halves
 * separately misses any match crossing the split -- which would make a search
 * fail only when the cursor sat inside the word being looked for. Several of
 * these put the cursor exactly there on purpose.
 *
 * Run under ASan (see test/run.sh).
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "editor.h"
#include "text_buffer.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-52s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-52s got %d, want %d\n", name, got, want);
        failures++;
    }
}

static void check_at(const char* name, bool found, tb_pos at,
                     int line, int x) {
    if (found && at.line == line && at.x == x) {
        fprintf(stderr, "PASS  %-52s (%d,%d)\n", name, at.line, at.x);
    } else if (!found) {
        fprintf(stderr, "FAIL  %-52s not found, want (%d,%d)\n",
                name, line, x);
        failures++;
    } else {
        fprintf(stderr, "FAIL  %-52s got (%d,%d), want (%d,%d)\n",
                name, at.line, at.x, line, x);
        failures++;
    }
}

int main(void) {
    stub_discard_output();

    static const char DOC[] =
        "the quick brown fox\r\n"
        "jumps over the lazy dog\r\n"
        "THE END\r\n";
    stub_file_reset();
    stub_file_set_content(DOC, (int) sizeof(DOC) - 1);
    editor ed;
    check("an editor to search in", ed_init(&ed, 8, "f.txt") != NULL, 1);
    text_buffer* tb = &ed.buf_;

    tb_pos at;
    const tb_pos top = {1, 0};

    /* The plain case. */
    check_at("a word on the first line",
             tb_find(tb, "quick", 5, top, true, &at), at, 1, 4);
    check_at("a word further down",
             tb_find(tb, "lazy", 4, top, true, &at), at, 2, 15);

    /* Case-insensitive, both directions of difference. */
    check_at("a lowercase needle finds uppercase text",
             tb_find(tb, "the end", 7, top, true, &at), at, 3, 0);
    check_at("an uppercase needle finds lowercase text",
             tb_find(tb, "QUICK", 5, top, true, &at), at, 1, 4);

    /* The gap. Putting the cursor inside the word being searched for is what
     * breaks a raw scan of the buffer: the text is split at the cursor, so
     * "quick" is stored as "qu" and "ick" with the gap between them. */
    tb_seek(tb, (tb_pos){1, 6});
    check_at("a match with the cursor inside it",
             tb_find(tb, "quick", 5, top, true, &at), at, 1, 4);
    tb_seek(tb, (tb_pos){2, 17});
    check_at("  and again on another line",
             tb_find(tb, "lazy", 4, top, true, &at), at, 2, 15);

    /* Searching forward from a match must move on, not sit on it. */
    tb_pos from = {1, 0};
    check_at("the first 'the'", tb_find(tb, "the", 3, from, true, &at), at, 1, 0);
    from = at;
    from.x++;
    check_at("  then the next one", tb_find(tb, "the", 3, from, true, &at), at, 2, 11);

    /* Wrapping, forwards and backwards. */
    from = (tb_pos){3, 0};
    from.x++;
    check_at("searching past the end wraps to the top",
             tb_find(tb, "quick", 5, from, true, &at), at, 1, 4);
    from = (tb_pos){1, 0};
    check_at("searching back past the start wraps to the end",
             tb_find(tb, "END", 3, from, false, &at), at, 3, 4);

    /* A match on the starting line but *before* where the search began. Found
     * only by wrapping all the way round and coming back to that line, which is
     * why the sweep runs one pass longer than there are lines. */
    from = (tb_pos){1, 10};
    check_at("wrapping comes back to the line it started on",
             tb_find(tb, "quick", 5, from, true, &at), at, 1, 4);

    /* Backwards finds the nearest match behind, not the first in the file.
     * From (2,10) the "the" at (2,11) is ahead, so the answer is on line 1. */
    from = (tb_pos){2, 10};
    check_at("backwards finds the nearest match behind",
             tb_find(tb, "the", 3, from, false, &at), at, 1, 0);

    /* Searching backwards from column 0 has nothing behind it on that line.
     * The caller passes x - 1, which is -1 there -- and that must mean "skip
     * this line", not "scan all of it", or a backwards search would find a
     * match ahead of the cursor on the line it started from. */
    from = (tb_pos){3, -1};
    check_at("backwards from column 0 skips its own line",
             tb_find(tb, "the", 3, from, false, &at), at, 2, 11);

    /* No match at all, and the degenerate needles. */
    check("something absent is not found",
          tb_find(tb, "zebra", 5, top, true, &at) ? 1 : 0, 0);
    check("an empty needle finds nothing",
          tb_find(tb, "", 0, top, true, &at) ? 1 : 0, 0);
    check("a needle longer than the document finds nothing",
          tb_find(tb, "the quick brown fox jumps over", 30, top, true, &at) ? 1 : 0, 0);

    /* A match never spans a line break: "fox jumps" is contiguous in the file
     * but not on one line. */
    check("a match does not span a line break",
          tb_find(tb, "fox\r\njumps", 10, top, true, &at) ? 1 : 0, 0);

    /* Searching must not disturb the document or where the cursor is. */
    tb_seek(tb, (tb_pos){2, 5});
    const tb_pos before = tb_tell(tb);
    tb_find(tb, "END", 3, top, true, &at);
    const tb_pos after = tb_tell(tb);
    check("searching leaves the cursor alone", tb_cmp(before, after), 0);
    {
        int sz = 0;
        tb_seek(tb, (tb_pos){1, 0});
        const char* line = tb_suffix(tb, &sz);
        check("  and the document intact",
              sz == 19 && memcmp(line, "the quick brown fox", 19) == 0, 1);
        tb_seek(tb, before);
    }

    ed_destroy(&ed);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
