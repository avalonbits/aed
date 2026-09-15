/*
 * A grammar, and the thing that runs it.
 *
 * Rules are tried in order at each position and the first to match wins, so
 * order in the file is meaning: a comment marker has to come before anything
 * that could appear inside a comment.
 *
 * The map in these tests is one letter a byte, from the class each column ended
 * up in -- T text, C comment, S string, N number, K keyword, Y type, P
 * preprocessor, L label, O operator. It reads like the line under it, which is
 * what makes a wrong answer obvious rather than arithmetic.
 */

#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "syntax.h"

static int failures = 0;

static void check(const char* name, int got, int want) {
    if (got == want) {
        fprintf(stderr, "PASS  %-54s got %d\n", name, got);
    } else {
        fprintf(stderr, "FAIL  %-54s got %d, want %d\n", name, got, want);
        failures++;
    }
}

/* One letter a byte, from the class each column landed in. */
static const char CLS[] = "TCSNKYPLO";

static const char* lexed(const syntax* g, const char* line) {
    static tok_run runs[96];
    static char map[256];
    const int len = (int) strlen(line);
    const int n = syn_lex(g, line, len, runs, 96);
    int c = 0;
    for (int i = 0; i < n; i++) {
        while (c < runs[i].end && c < (int) sizeof(map) - 1) {
            map[c++] = CLS[(int) runs[i].cls];
        }
    }
    map[c] = 0;

    return map;
}

static void check_lex(const char* line, const syntax* g, const char* want) {
    const char* got = lexed(g, line);
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %-42s %s\n", line, got);
    } else {
        fprintf(stderr, "FAIL  %-42s %s\n%*swant %s\n", line, got, 44, "", want);
        failures++;
    }
}

static bool load_from(syntax* g, const char* text) {
    stub_file_reset();
    stub_file_add("/g.cfg", text, (int) strlen(text));
    syn_clear(g);

    return syn_load(g, "/g.cfg");
}

int main(void) {
    stub_discard_output();
    static syntax g;

    /* --- a comment marker the host format would have eaten --- */
    {
        /*
         * The one that nearly shipped wrong. ';' begins a comment in assembly
         * and it begins one in an INI file too, so `eol ;` lost its literal
         * before the rule ever saw it -- the grammar loaded with one rule fewer
         * and every comment in every assembly file painted as ordinary text.
         *
         * Quoting is the fix, and ini_read_line now knows not to cut inside
         * quotes. Both halves are checked: that the quoted form works, and that
         * the bare form is still eaten, because if the reader ever stops
         * cutting on ';' altogether then every settings file comment breaks.
         */
        check("a grammar naming ';' as its comment marker",
              load_from(&g,
                        "[syntax]\nname = t\nextensions = .t\n"
                        "[match]\ncomment.line = eol ';'\n") ? 1 : 0, 1);
        check("  loads the rule", g.nrules, 1);
        check_lex("ld a, 1 ; and the rest", &g, "TTTTTTTTCCCCCCCCCCCCCC");

        check("the same rule written bare still loads",
              load_from(&g,
                        "[syntax]\nname = t\nextensions = .t\n"
                        "[match]\ncomment.line = eol ;\ncomment.block = eol //\n")
              ? 1 : 0, 1);
        check("  but the bare ';' was taken for a comment, so only one rule",
              g.nrules, 1);
    }

    /* --- the real assembly grammar, on real assembly --- */
    {
        static char text[4096];
        int n = 0;
        {
            /* The file AED ships, read through the stub the way the editor
             * would read it off the card. */
            FILE* f = fopen("config/aed/syntax/asm.cfg", "rb");
            if (f == NULL) {
                fprintf(stderr, "FAIL  cannot open config/aed/syntax/asm.cfg\n");

                return 1;
            }
            n = (int) fread(text, 1, sizeof(text) - 1, f);
            fclose(f);
            text[n] = 0;
        }
        stub_file_reset();
        stub_file_add("/asm.cfg", text, n);
        syn_clear(&g);
        check("the assembly grammar loads", syn_load(&g, "/asm.cfg") ? 1 : 0, 1);
        check("  with every rule", g.nrules, 8);
        check("  and its words", g.noffs > 100 ? 1 : 0, 1);

        check("  it claims .asm", syn_covers(&g, "hello.asm") ? 1 : 0, 1);
        check("    and .s whatever the case", syn_covers(&g, "boot.S") ? 1 : 0, 1);
        check("    and a path with dots in its directories",
              syn_covers(&g, "/my.stuff/boot.asm") ? 1 : 0, 1);
        check("  but not .c", syn_covers(&g, "main.c") ? 1 : 0, 0);
        check("    nor a name with no extension",
              syn_covers(&g, "Makefile") ? 1 : 0, 0);

        /* Lines from the demo that ships with the emulator. */
        check_lex("\t.org $40000",     &g, "TPPPPTNNNNNN");
        check_lex("\tjp start",        &g, "TKKTTTTTT");
        check_lex("\t.db \"MOS\"",     &g, "TPPPTSSSSS");
        check_lex("\t.db 0 ; version", &g, "TPPPTNTCCCCCCCCC");
        check_lex("start:",            &g, "LLLLLT");
        check_lex("\tpush ix",         &g, "TKKKKTYY");
        check_lex("\trst.lil $18",     &g, "TKKKKKKKTNNN");

        /* A second ';' is inside the first comment, not a new one. */
        check_lex("\tld hl, 0 ; a ; b", &g, "TKKTYYTTNTCCCCCCC");

        /* LD and ld are the same instruction. */
        check_lex("\tLD HL, 0",        &g, "TKKTYYTTN");

        /* A label only where a line starts: `start` further along is a
         * reference to one, and the grammar has no way to know it. */
        check_lex("nope:\tjp nope",    &g, "LLLLTTKKTTTTT");
    }

    /* --- what the lexer must never do --- */
    {
        check("a grammar with words and numbers",
              load_from(&g,
                        "[syntax]\nname = t\nextensions = .t\n"
                        "[match]\nkeyword.control = words if do\n"
                        "constant.numeric = number\n") ? 1 : 0, 1);

        /* A keyword inside a longer word is not a keyword. */
        check_lex("ifdef", &g, "TTTTT");
        check_lex("doing", &g, "TTTTT");
        check_lex("xif",   &g, "TTT");
        check_lex("if do", &g, "KKTKK");

        /* A number's digits do not leak into the word beside them. */
        check_lex("1st",   &g, "TTT");
        check_lex("a1",    &g, "TT");
        check_lex("1 2",   &g, "NTN");

        /*
         * Runs of one class merge, so the painting pays one colour change and
         * not one a byte -- the property test_theme counts in changes.
         *
         * "a, b" is the case that shows it: four things go in, all of them
         * text -- a word, a comma, a space, a word -- and one run has to come
         * out. "if if if" cannot show it, because its classes alternate and
         * nothing adjacent is ever the same; that was the first version of
         * this check and the merge could be deleted without it noticing.
         */
        static tok_run runs[96];
        check("  four text tokens in a row are one run",
              syn_lex(&g, "a, b", 4, runs, 96), 1);
        check("  and alternating ones are not merged",
              syn_lex(&g, "if if if", 8, runs, 96), 5);
    }

    /* --- a word rule will not match half of a word --- */
    {
        /*
         * Reaching this needs a rule that stops inside a word, which the
         * ordinary path never does: an unclaimed byte takes its whole word, so
         * the next position is always a boundary. A span that closes mid-word
         * is what gets there.
         *
         * The span has to *begin* outside a word to get there, since the
         * default would otherwise have swallowed the word whole before the
         * span was ever tried. `span '(' 'd'` over "x(abdef" consumes "(abd"
         * and leaves the cursor on the "e" with a word character behind it.
         * The boundary test is what stops that "ef" being read as the whole
         * word it is not.
         */
        check("a grammar whose span can stop inside a word",
              load_from(&g,
                        "[syntax]\nname = t\nextensions = .t\n"
                        "[match]\nstring.quoted.double = span '(' 'd'\n"
                        "keyword.control = words ef\n") ? 1 : 0, 1);
        check_lex("x(abdef", &g, "TSSSSTT");
        check("  and a whole `ef` is still a keyword",
              strcmp(lexed(&g, "ef"), "KK") == 0 ? 1 : 0, 1);
    }

    /* --- a grammar that will not read leaves the last one alone --- */
    {
        check("a grammar loads", load_from(&g,
                "[syntax]\nname = keep\nextensions = .k\n"
                "[match]\ncomment.line = eol '#'\n") ? 1 : 0, 1);
        check("  a file that is not there is refused",
              syn_load(&g, "/nowhere.cfg") ? 1 : 0, 0);
        check("    and the grammar already loaded is untouched",
              strcmp(g.name, "keep") == 0 ? 1 : 0, 1);

        /* Loaded directly rather than through load_from, which clears the
         * grammar first -- that would destroy the very thing being checked,
         * and made the first version of this test pass for the wrong reason. */
        stub_file_reset();
        stub_file_add("/empty.cfg", "[syntax]\nname = empty\nextensions = .e\n", 38);
        check("  a grammar with no rules at all is refused",
              syn_load(&g, "/empty.cfg") ? 1 : 0, 0);
        check("    leaving the last good one", strcmp(g.name, "keep") == 0 ? 1 : 0, 1);
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
