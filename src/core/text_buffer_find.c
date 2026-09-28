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
 * Finding text in a document, including the part of it on disk.
 *
 * The scan itself is a tight loop over a flat buffer; what surrounds it is the
 * machinery for feeding the whole document through that loop a line at a time,
 * in either direction, wrapping at the ends.
 */
#include "text_buffer.h"
#include "text_buffer_int.h"

#include <stdbool.h>
#include <string.h>

bool tbi_isstop(char ch) {
    switch (ch) {
        case '[':
        case ']':
        case '(':
        case ')':
        case '<':
        case '>':
        case ' ':
        case '\t':
        case ';':
        case ':':
        case '.':
        case ',':
        case '@':
        case '!':
        case '#':
        case '\\':
        case '/':
            return true;
        default:
            return false;
    }
    return false;
}

// How many line feeds are in a run. The same CPIR, for the counters that do not
// want the lengths.
int tbi_count_lines(const char* buf, int n) {
    int lines = 0;
    while (n > 0) {
        const char* nl = (const char*) memchr(buf, '\n', (size_t) n);
        if (nl == NULL) {
            break;
        }
        lines++;
        n -= (int) (nl - buf) + 1;
        buf = nl + 1;
    }

    return lines;
}

// ASCII case folding. The Agon's character set beyond 127 is not a case-mapped
// alphabet, so anything else is left alone rather than guessed at.
static char fold(char ch) {
    return (ch >= 'A' && ch <= 'Z') ? (char) (ch + ('a' - 'A')) : ch;
}

/*
 * Finding in a paged document.
 *
 * The in-memory search walks a line at a time on a tb_copy, and a walker cannot
 * slide, so it searches the window and stops. On a 160,000 byte document paged
 * into 64 KiB, a match planted a hundred lines from the end reported "not
 * found" -- and reported it the same way a real absence does.
 *
 * So a paged document is searched by streaming it, once, from the beginning.
 * One pass answers all four questions a search can ask, because forwards and
 * backwards differ only in which match is kept:
 *
 *   forward  -- the first match at or after `from`, else the first anywhere
 *   backward -- the last match at or before `from`, else the last anywhere
 *
 * which is what "wrapping once around the document" means. Collecting all four
 * costs nothing over collecting one, and saves the second pass a wrap would
 * otherwise need.
 *
 * Knuth-Morris-Pratt rather than the in-memory scan, because there is no line
 * to back up over: the bytes arrive once, in order, and a partial match has to
 * survive between chunks. The failure table is built from the needle each time
 * -- 64 bytes of work against a document's worth of reading.
 */
static bool match_at(const char* hay, const char* needle, int nsz) {
    for (int i = 0; i < nsz; i++) {
        if (fold(hay[i]) != fold(needle[i])) {
            return false;
        }
    }

    return true;
}

// Where `needle` sits in one line, or -1. `from` is the first index tried going
// forward, or the last one going backward; negative means the whole line.
#define TB_FIND_MAX 64      // editor.find_ is char[64]; nothing longer exists

static int scan_line(const char* hay, int hsz, const char* needle, int nsz,
                     int from, bool forward) {
    if (hay == NULL || nsz > hsz) {
        return -1;
    }
    const int last = hsz - nsz;

    // The needle's first character, folded once. Almost every position in a
    // document fails on it, and match_at is a real call -- a call, a frame and
    // a return -- so asking it was costing one of those per byte of the
    // document. Testing the cheapest term first and only then paying for the
    // rest is what a search should do; here it is the whole of the work.
    //
    // Both loop bounds are unsigned. i and last are both non-negative -- the
    // nsz > hsz check above is what makes last so -- and a signed comparison on
    // this target carries a `call pe, __setflag` to repair the flags, which an
    // unsigned one does not.
    const char n0 = fold(needle[0]);
    if (forward) {
        const unsigned stop = (unsigned) last;
        for (unsigned i = (unsigned)(from < 0 ? 0 : from); i <= stop; i++) {
            if (fold(hay[i]) == n0 && match_at(hay + i, needle, nsz)) {
                return (int) i;
            }
        }
    } else {
        for (unsigned i = (unsigned)((from < 0 || from > last) ? last : from) + 1;
             i-- != 0; ) {
            if (fold(hay[i]) == n0 && match_at(hay + i, needle, nsz)) {
                return (int) i;
            }
        }
    }

    return -1;
}

/*
 * The same scan, over a line that arrives in two pieces.
 *
 * Three places a match can be, and they are three disjoint ranges of starting
 * position: wholly inside the first run, across the join, or wholly inside the
 * second. The two runs are the contiguous scan again, unchanged -- the cost of
 * a search is in those and they stay a tight loop over a flat buffer.
 *
 * Across the join there are at most nsz - 1 starting positions, because a match
 * starting any earlier ends before the join and a match starting at the join is
 * wholly in the second run. So the bytes either side are copied into a window
 * and scanned there. That is the only copying a search does, and it is bounded
 * by the needle rather than by the line: 126 bytes at the most.
 *
 * Positions in and out are indices into the line as a whole, the way the caller
 * counts columns. `forward` decides which end the three ranges are tried from,
 * and they are strictly ordered, so the first hit found is the nearest one.
 */
static int scan_split(const char* pre, int psz, const char* suf, int ssz,
                      const char* needle, int nsz, int from, bool forward);

int tb_scan_split(const char* pre, int psz, const char* suf, int ssz,
                  const char* needle, int nsz, int from, bool forward) {
    return scan_split(pre, psz, suf, ssz, needle, nsz, from, forward);
}

static int scan_split(const char* pre, int psz, const char* suf, int ssz,
                      const char* needle, int nsz, int from, bool forward) {
    if (psz <= 0) {
        return scan_line(suf, ssz, needle, nsz, from, forward);
    }
    if (ssz <= 0) {
        return scan_line(pre, psz, needle, nsz, from, forward);
    }
    if (nsz > psz + ssz) {
        return -1;
    }

    // A negative `from` is the caller saying "anywhere on this line", which is
    // a different thing from a position that happens to fall before one of the
    // runs. Keeping the two apart is the whole of the bookkeeping below.
    const bool anywhere = from < 0;

    // Static because a frame is addressed with a signed byte on this machine
    // and 126 of them would put every other local out of reach. A search is
    // already non-reentrant -- see find_pass.
    static char join[2 * (TB_FIND_MAX - 1)];

    // As much of each side as a crossing match could possibly touch.
    const int lead = (nsz - 1) < psz ? (nsz - 1) : psz;
    const int tail = (nsz - 1) < ssz ? (nsz - 1) : ssz;
    const int base = psz - lead;        // where the window starts in the line
    memcpy(join, pre + base, (size_t) lead);
    memcpy(join + lead, suf, (size_t) tail);

    int hit = -1;
    for (int part = 0; part < 3 && hit < 0; part++) {
        // Forwards: first run, join, second run. Backwards: the reverse. The
        // three ranges of starting position do not overlap and are in order,
        // so the first hit found is the nearest one to `from`.
        const int which = forward ? part : 2 - part;

        if (which == 0) {
            hit = scan_line(pre, psz, needle, nsz, anywhere ? -1 : from,
                            forward);
        } else if (which == 2) {
            const int f = anywhere ? -1 : from - psz;
            if (!anywhere && f < 0 && !forward) {
                continue;       // the whole run is past where to look
            }
            const int in = scan_line(suf, ssz, needle, nsz,
                                     (!anywhere && f < 0) ? 0 : f, forward);
            hit = in < 0 ? -1 : psz + in;
        } else {
            // Only the starts that really cross count: the others belong to
            // one of the two runs and are found there, in the right order.
            for (int j = forward ? 0 : lead - 1;
                 forward ? j < lead : j >= 0;
                 forward ? j++ : j--) {
                if (j + nsz <= lead) {
                    continue;           // ends before the join
                }
                if (j + nsz > lead + tail) {
                    continue;           // runs past what the line holds
                }
                const int line_at = base + j;
                if (!anywhere && (forward ? line_at < from : line_at > from)) {
                    continue;
                }
                if (match_at(join + j, needle, nsz)) {
                    hit = line_at;
                    break;
                }
            }
        }
    }

    return hit;
}

// One of these, at file scope. On this machine a field of a file-scope object
// is addressed absolutely -- the address is a constant in the instruction --
// where the same field through a pointer parameter is a load of the pointer out
// of the frame and then a displacement. A search touches these fields once per
// byte of the document, so that difference is the search.
//
// Which makes a search non-reentrant, and it always was: the needle is one
// buffer on the editor and nothing searches inside a search.
typedef struct _find_pass {
    const char* needle;
    int nsz;
    int fail[TB_FIND_MAX];

    tb_pos from;
    int m;              // needle characters matched so far
    int line;           // the line the next byte belongs to
    int x;              // its column
    bool held_cr;

    tb_pos at_from;     // first match at or after `from`
    bool has_at_from;
    tb_pos first;       // first match anywhere
    bool has_first;
    tb_pos to_from;     // last match at or before `from`
    bool has_to_from;
    tb_pos last;        // last match anywhere
    bool has_last;
} find_pass;

static find_pass fp;

static void fp_build(void) {
    fp.fail[0] = 0;
    for (int i = 1; i < fp.nsz; i++) {
        int k = fp.fail[i - 1];
        while (k > 0 && fold(fp.needle[i]) != fold(fp.needle[k])) {
            k = fp.fail[k - 1];
        }
        if (fold(fp.needle[i]) == fold(fp.needle[k])) {
            k++;
        }
        fp.fail[i] = k;
    }
}

static void fp_hit(tb_pos p) {
    if (!fp.has_first) {
        fp.first = p;
        fp.has_first = true;
    }
    fp.last = p;
    fp.has_last = true;

    // At or after `from`, for a forward search. Only the first one counts.
    if (!fp.has_at_from
            && (p.line > fp.from.line
                || (p.line == fp.from.line && p.x >= fp.from.x))) {
        fp.at_from = p;
        fp.has_at_from = true;
    }
    // At or before it, for a backward one. The last such is the answer, so
    // this keeps overwriting until the positions run past `from`.
    if (p.line < fp.from.line
            || (p.line == fp.from.line && p.x <= fp.from.x)) {
        fp.to_from = p;
        fp.has_to_from = true;
    }
}

static bool find_sink(void* ctx, const char* buf, int sz) {
    (void) ctx;     // fp is at file scope; see the note on find_pass

    for (int i = 0; i < sz; i++) {
        char c = buf[i];
        if (c == '\r' && !fp.held_cr) {
            fp.held_cr = true;
            continue;
        }
        if (c == '\n') {
            // A match never spans a break, so nothing carries across one.
            fp.held_cr = false;
            fp.m = 0;
            fp.line++;
            fp.x = 0;
            continue;
        }
        if (fp.held_cr) {
            fp.held_cr = false;     // a stray carriage return is a character
            i--;                    // and this byte is dealt with next time
            c = '\r';
        }

        // The Knuth-Morris-Pratt step, written here rather than called. It runs
        // once per byte of the document, and a call, a frame and a return on
        // top of it doubled the cost of a search.
        const char f = fold(c);
        while (fp.m > 0 && f != fold(fp.needle[fp.m])) {
            fp.m = fp.fail[fp.m - 1];
        }
        if (f == fold(fp.needle[fp.m])) {
            fp.m++;
        }
        if (fp.m == fp.nsz) {
            const tb_pos p = { fp.line, fp.x - fp.nsz + 1 };
            fp_hit(p);
            fp.m = fp.fail[fp.m - 1];
        }
        fp.x++;
    }

    return true;
}

static bool find_stream(text_buffer* tb, const char* needle, int nsz,
                       tb_pos from, bool forward, tb_pos* at) {
    fp.needle = needle;
    fp.nsz = nsz;
    fp.from = from;
    fp.m = 0;
    fp.line = 1;
    fp.x = 0;
    fp.held_cr = false;
    fp.has_at_from = false;
    fp.has_first = false;
    fp.has_to_from = false;
    fp.has_last = false;
    fp_build();

    if (!tbi_doc_stream(tb, find_sink, NULL)) {
        return false;
    }
    if (forward) {
        if (fp.has_at_from) {
            *at = fp.at_from;

            return true;
        }
        if (fp.has_first) {
            *at = fp.first;     // wrapped

            return true;
        }

        return false;
    }
    if (fp.has_to_from) {
        *at = fp.to_from;

        return true;
    }
    if (fp.has_last) {
        *at = fp.last;          // wrapped

        return true;
    }

    return false;
}

bool tb_find(text_buffer* tb, const char* needle, int nsz, tb_pos from,
             bool forward, tb_pos* at) {
    if (needle == NULL || nsz <= 0 || at == NULL || nsz > TB_FIND_MAX) {
        return false;
    }

    if (tb->paged_) {
        // Streaming is the only way past the window, and it walks every byte
        // through the line and column bookkeeping to do it. Measured against
        // the scan below on a document that fits: 338 centiseconds to 264 on
        // a full search that misses. So the scan stays for the documents it
        // can answer, which is most of them.
        return find_stream(tb, needle, nsz, from, forward, at);
    }

    // On a copy, which is safe because moving a gap duplicates rather than
    // destroys: going forward copies high bytes down, going back copies low
    // bytes up, and in both cases the bytes outside the moved region keep their
    // values. The real cursor reads the same before and after.
    text_buffer cp;
    tb_copy(&cp, tb);
    tb_seek(&cp, (tb_pos){from.line, 0});

    const int total = tb_ymax(tb);
    int line = from.line;
    int start = from.x;
    bool first = true;

    // One pass per line, plus one more for the part of the starting line the
    // first pass skipped over.
    for (int n = 0; n <= total; n++) {
        // The whole line, in the one or two runs it is held in. The walker's
        // gap sits wherever the step left it, so from the second line on the
        // line it is reading is split at that column -- which is why the scan
        // takes a pair.
        const split_line ln = tb_curr_line(&cp);
        // A caller searching backwards hands us x - 1, which is negative when
        // the cursor sits in column 0 -- and that means nothing on this line is
        // behind it, not that the whole line is fair game.
        const int hit = (first && !forward && start < 0)
            ? -1
            : scan_split(ln.prefix_, ln.psz_, ln.suffix_, ln.ssz_,
                         needle, nsz, first ? start : -1, forward);
        first = false;
        if (hit >= 0) {
            at->line = line;
            at->x = hit;

            return true;
        }

        int next = forward ? line + 1 : line - 1;
        const bool wrapped = next > total || next < 1;
        if (wrapped) {
            next = forward ? 1 : total;
            // Only the wrap seeks. Stepping a line at a time keeps the whole
            // sweep linear; a seek per line would move the gap from wherever it
            // is on every one of them.
            tb_seek(&cp, (tb_pos){next, 0});
        } else if (forward) {
            tb_down(&cp);
        } else {
            tb_up(&cp);
        }
        line = next;
    }

    return false;
}

bool tb_is_word_stop(char ch) {
    return tbi_isstop(ch);
}
