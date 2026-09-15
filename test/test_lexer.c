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
    const int n = syn_lex(g, line, len, SYN_STATE_NONE, NULL, runs, 96);
    int c = 0;
    for (int i = 0; i < n; i++) {
        while (c < runs[i].end && c < (int) sizeof(map) - 1) {
            map[c++] = CLS[(int) runs[i].cls];
        }
    }
    map[c] = 0;

    return map;
}

/* The same map, for a line that begins part way through something. */
static const char* lexed_in(const syntax* g, const char* line, int in,
                            int* out_state) {
    static tok_run runs[96];
    static char map[256];
    const int len = (int) strlen(line);
    const int n = syn_lex(g, line, len, in, out_state, runs, 96);
    int c = 0;
    for (int i = 0; i < n; i++) {
        while (c < runs[i].end && c < (int) sizeof(map) - 1) {
            map[c++] = CLS[(int) runs[i].cls];
        }
    }
    map[c] = 0;

    return map;
}

static void check_map(const char* what, const char* got, const char* want) {
    if (strcmp(got, want) == 0) {
        fprintf(stderr, "PASS  %-42s %s\n", what, got);
    } else {
        fprintf(stderr, "FAIL  %-42s %s\n%*swant %s\n", what, got, 44, "",
                want);
        failures++;
    }
}

/* A document as an array, for the lookback. */
static const char** doc_lines = NULL;
static int doc_n = 0;
static int doc_reads = 0;

static int doc_get(void* ctx, int y, char* buf, int max) {
    (void) ctx;
    if (y < 0 || y >= doc_n) {
        return -1;
    }
    doc_reads++;
    int n = (int) strlen(doc_lines[y]);
    if (n > max) {
        n = max;
    }
    memcpy(buf, doc_lines[y], (size_t) n);

    return n;
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
              syn_lex(&g, "a, b", 4, SYN_STATE_NONE, NULL, runs, 96), 1);
        check("  and alternating ones are not merged",
              syn_lex(&g, "if if if", 8, SYN_STATE_NONE, NULL, runs, 96), 5);
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

    /* --- a block comment crosses a line, and a string does not --- */
    {
        /*
         * The whole of what step 4 adds. A `span` marked `multiline` that does
         * not close leaves a state behind; the next line starts inside it and
         * the rules never see those columns.
         *
         * The string rule in the same grammar is deliberately not multiline,
         * so the two cases sit side by side: one stray quote colours one line,
         * one stray slash-star colours until it is closed.
         */
        check("a grammar with a multiline span",
              load_from(&g,
                        "[syntax]\nname = t\nextensions = .t\n[match]\n"
                        "comment.block = span '/*' '*/' multiline\n"
                        "string.quoted.double = span '\"' '\"' escape \\\n"
                        "keyword.control = words if\n") ? 1 : 0, 1);
        check("  three rules", g.nrules, 3);
        check("  the block comment may cross", g.rules[0].multiline ? 1 : 0, 1);
        check("  the string may not", g.rules[1].multiline ? 1 : 0, 0);

        int st = -1;
        check_map("if /* opens", lexed_in(&g, "if /* opens", SYN_STATE_NONE, &st),
                  "KKTCCCCCCCC");
        check("  and the line is left inside it", st != SYN_STATE_NONE, 1);

        int st2 = -1;
        check_map("  still inside, if ignored",
                  lexed_in(&g, "if still inside", st, &st2),
                  "CCCCCCCCCCCCCCC");
        check("    and stays inside", st2 == st, 1);

        int st3 = -1;
        check_map("  closes */ then if",
                  lexed_in(&g, "closes */ if", st, &st3), "CCCCCCCCCTKK");
        check("    and the line ends clean", st3, SYN_STATE_NONE);

        int st4 = -1;
        check_map("  an unterminated \" does not carry",
                  lexed_in(&g, "if \"open", SYN_STATE_NONE, &st4), "KKTSSSSS");
        check("    leaving no state", st4, SYN_STATE_NONE);

        /*
         * A state is an index into the rules, and the rules move when a
         * grammar is reloaded. One that names a rule this grammar does not
         * have, or names a rule that cannot cross a line, is dropped rather
         * than trusted -- otherwise a reload would colour by a stale index.
         */
        int st5 = -1;
        check_map("  a state past the last rule is dropped",
                  lexed_in(&g, "if x", 99, &st5), "KKTT");
        check("    and leaves none", st5, SYN_STATE_NONE);
        int st6 = -1;
        check_map("  a state naming the string rule is dropped",
                  lexed_in(&g, "if x", 2, &st6), "KKTT");
        check("    and leaves none", st6, SYN_STATE_NONE);
    }

    /* --- escape and multiline may be written in either order --- */
    {
        check("multiline before escape",
              load_from(&g,
                        "[syntax]\nname = t\nextensions = .t\n[match]\n"
                        "string.quoted.double = span '<' '>' multiline escape \\\n")
              ? 1 : 0, 1);
        check("  both are read", (g.rules[0].multiline ? 2 : 0)
              + (g.rules[0].escape == '\\' ? 1 : 0), 3);

        check("escape before multiline",
              load_from(&g,
                        "[syntax]\nname = t\nextensions = .t\n[match]\n"
                        "string.quoted.double = span '<' '>' escape \\ multiline\n")
              ? 1 : 0, 1);
        check("  both are read", (g.rules[0].multiline ? 2 : 0)
              + (g.rules[0].escape == '\\' ? 1 : 0), 3);

        check("a span with neither is still a span",
              load_from(&g,
                        "[syntax]\nname = t\nextensions = .t\n[match]\n"
                        "string.quoted.double = span '<' '>'\n") ? 1 : 0, 1);
        check("  and does not cross a line", g.rules[0].multiline ? 1 : 0, 0);
    }

    /* --- the lookback, for a view that jumped --- */
    {
        /*
         * Scrolling carries the state forward for nothing; a jump arrives with
         * none. syn_state_before reads back at most SYN_LOOKBACK lines and
         * lexes forward, which is the whole of the design -- no per-line state
         * is stored anywhere.
         */
        static const char* d[] = {
            "int a;",           /* 0 */
            "/* open",          /* 1 */
            "still",            /* 2 */
            "*/ int b;",        /* 3 */
            "int c;",           /* 4 */
        };
        doc_lines = d;
        doc_n = 5;

        check("a C-like grammar with a block comment",
              load_from(&g,
                        "[syntax]\nname = t\nextensions = .t\n[match]\n"
                        "comment.block = span '/*' '*/' multiline\n"
                        "storage.type = words int\n") ? 1 : 0, 1);

        char buf[SYN_SCAN_MAX];
        doc_reads = 0;
        check("  line 0 starts clean",
              syn_state_before(&g, 0, doc_get, NULL, buf, sizeof(buf)),
              SYN_STATE_NONE);
        check("    without reading anything", doc_reads, 0);

        check("  line 2 starts inside the comment",
              syn_state_before(&g, 2, doc_get, NULL, buf, sizeof(buf))
              != SYN_STATE_NONE, 1);
        check("  line 3 starts inside it too",
              syn_state_before(&g, 3, doc_get, NULL, buf, sizeof(buf))
              != SYN_STATE_NONE, 1);
        check("  line 4 starts clean again",
              syn_state_before(&g, 4, doc_get, NULL, buf, sizeof(buf)),
              SYN_STATE_NONE);

        /* And the state it finds is the one that paints the row. */
        const int at2 = syn_state_before(&g, 2, doc_get, NULL, buf,
                                         sizeof(buf));
        check_map("  so line 2 paints as comment",
                  lexed_in(&g, d[2], at2, NULL), "CCCCC");
        check_map("  and without it would not",
                  lexed_in(&g, d[2], SYN_STATE_NONE, NULL), "TTTTT");

        /*
         * A grammar that cannot cross a line can only answer NONE, so it is
         * answered without touching the document at all. That is what keeps a
         * jump in an assembly file exactly as cheap as it is today.
         */
        check("a grammar with nothing that crosses",
              load_from(&g,
                        "[syntax]\nname = t\nextensions = .t\n[match]\n"
                        "comment.line = eol ';'\n") ? 1 : 0, 1);
        doc_reads = 0;
        check("  answers NONE for any line",
              syn_state_before(&g, 4, doc_get, NULL, buf, sizeof(buf)),
              SYN_STATE_NONE);
        check("    having read no lines at all", doc_reads, 0);
    }

    /* --- the lookback is bounded --- */
    {
        /*
         * A comment opened further back than SYN_LOOKBACK is not found, and
         * the point of the test is that the scan stops rather than walking to
         * the top of a large file. The row paints plainly until the view is
         * scrolled through the opening, which is the trade the design names.
         */
        static const char* d[SYN_LOOKBACK + 60];
        d[0] = "/* opened a long way back";
        for (int i = 1; i < SYN_LOOKBACK + 60; i++) {
            d[i] = "still inside";
        }
        doc_lines = d;
        doc_n = SYN_LOOKBACK + 60;

        check("a grammar with a block comment",
              load_from(&g,
                        "[syntax]\nname = t\nextensions = .t\n[match]\n"
                        "comment.block = span '/*' '*/' multiline\n") ? 1 : 0, 1);

        char buf[SYN_SCAN_MAX];
        doc_reads = 0;
        check("  a line just inside the lookback is found",
              syn_state_before(&g, SYN_LOOKBACK, doc_get, NULL, buf,
                               sizeof(buf)) != SYN_STATE_NONE, 1);
        check("    reading exactly the lookback", doc_reads, SYN_LOOKBACK);

        doc_reads = 0;
        check("  one line further back is missed",
              syn_state_before(&g, SYN_LOOKBACK + 1, doc_get, NULL, buf,
                               sizeof(buf)), SYN_STATE_NONE);
        check("    still reading only the lookback", doc_reads, SYN_LOOKBACK);
    }

    /* --- the C grammar AED ships --- */
    {
        static char text[4096];
        int n = 0;
        {
            FILE* f = fopen("config/aed/syntax/c.cfg", "rb");
            if (f == NULL) {
                fprintf(stderr, "FAIL  cannot open config/aed/syntax/c.cfg\n");
                failures++;
            } else {
                n = (int) fread(text, 1, sizeof(text), f);
                fclose(f);
            }
        }
        stub_file_reset();
        stub_file_add("/c.cfg", text, n);
        syn_clear(&g);
        check("the C grammar loads", syn_load(&g, "/c.cfg") ? 1 : 0, 1);
        check("  and claims a .c file", syn_covers(&g, "/src/main.c") ? 1 : 0, 1);
        check("  and a .h file", syn_covers(&g, "/src/main.h") ? 1 : 0, 1);
        check("  and a .hpp file", syn_covers(&g, "/x.hpp") ? 1 : 0, 1);
        check("  and leaves an .asm file alone",
              syn_covers(&g, "/boot.asm") ? 1 : 0, 0);
        check("  C is case sensitive", g.nocase ? 1 : 0, 0);

        check_lex("int x = 42;", &g, "YYYTTTTTNNT");
        check_lex("return 0;", &g, "KKKKKKTNT");
        check_lex("// a comment", &g, "CCCCCCCCCCCC");
        check_lex("#include <stdio.h>", &g, "PPPPPPPPPPPPPPPPPP");
        check_lex("x = \"if 42\";", &g, "TTTTSSSSSSST");

        int st = -1;
        check_map("a block comment opens",
                  lexed_in(&g, "int a; /* why", SYN_STATE_NONE, &st),
                  "YYYTTTTCCCCCC");
        check("  and carries", st != SYN_STATE_NONE, 1);
        check_map("  the next line is all comment",
                  lexed_in(&g, "int b;", st, NULL), "CCCCCC");
        int st2 = -1;
        check_map("  until it closes",
                  lexed_in(&g, "*/ int c;", st, &st2), "CCTYYYTTT");
        check("    and then it is clean", st2, SYN_STATE_NONE);
    }

    /* --- a word rule needs the language's own idea of a word --- */
    {
        /*
         * `MID$` is one word in BASIC and two things in C. Without wordchars
         * the word run stops at the `D`, so the rule can never match what it
         * names -- which is a rule that loads, looks right in the file, and
         * silently colours nothing.
         */
        check("a grammar naming MID$ without saying $ is a word letter",
              load_from(&g,
                        "[syntax]\nname = t\nextensions = .t\n[match]\n"
                        "support.function = words MID$\n") ? 1 : 0, 1);
        check_lex("MID$", &g, "TTTT");

        check("the same grammar saying so",
              load_from(&g,
                        "[syntax]\nname = t\nextensions = .t\nwordchars = $%\n"
                        "[match]\nsupport.function = words MID$\n") ? 1 : 0, 1);
        check_lex("MID$", &g, "YYYY");
        check_lex("MID", &g, "TTT");
        check("  and the letters are kept", g.wordchars[0] == '$'
              && g.wordchars[1] == '%' ? 1 : 0, 1);
    }

    /* --- a comment marker that is a word has to sit on boundaries --- */
    {
        /*
         * The BASIC counterpart of the ';' problem: `REM` starts a comment, so
         * without a boundary check `REMOVE` starts one too and the rest of the
         * line vanishes into it. A punctuation marker has no boundary to
         * respect, which is why C and assembly are untouched.
         */
        check("a grammar whose comment marker is a word",
              load_from(&g,
                        "[syntax]\nname = t\nextensions = .t\ncase = insensitive\n"
                        "[match]\ncomment.line = eol 'REM'\n") ? 1 : 0, 1);
        check_lex("REM hi", &g, "CCCCCC");
        check_lex("rem hi", &g, "CCCCCC");
        check_lex("REMOVE", &g, "TTTTTT");
        check_lex("X REM", &g, "TTCCC");
        check_lex("XREM", &g, "TTTT");

        /*
         * The other half of the boundary, and it takes some arranging to
         * reach: the fallback consumes a whole word at a time, so a rule is
         * normally only ever tried at a boundary already. A span that closes
         * on a word character is the one thing that leaves the next position
         * in the middle of a word -- and there the marker has to be refused.
         */
        check("a grammar whose span closes on a word character",
              load_from(&g,
                        "[syntax]\nname = t\nextensions = .t\ncase = insensitive\n"
                        "[match]\ncomment.line = eol 'REM'\n"
                        "string.quoted.double = span '<' 'X'\n") ? 1 : 0, 1);
        check_lex("a<bXREM", &g, "TSSSTTT");
        check_lex("a<bX REM", &g, "TSSSTCCC");

        check("a grammar whose marker is punctuation",
              load_from(&g,
                        "[syntax]\nname = t\nextensions = .t\n[match]\n"
                        "comment.line = eol '//'\n") ? 1 : 0, 1);
        check_lex("a//b", &g, "TCCC");
    }

    /* --- numbers a language other than assembly writes --- */
    {
        check("a grammar with numbers",
              load_from(&g,
                        "[syntax]\nname = t\nextensions = .t\n[match]\n"
                        "constant.numeric = number\n") ? 1 : 0, 1);
        check_lex("&FF", &g, "NNN");
        check_lex("$FF", &g, "NNN");
        check_lex("10.5", &g, "NNNN");
        check_lex("3.14159", &g, "NNNNNNN");

        /*
         * `&foo` is why `&` could not simply join the prefix list: `f` is a hex
         * digit, so without a check that the run ends where a word would, this
         * reads as the number `&f` and the text `oo`.
         */
        check_lex("&foo", &g, "TTTT");
        check_lex("&", &g, "T");
        /* One point only, so a version is text and assembly's rst.lil is safe. */
        check_lex("1.0.2", &g, "TTTTT");
    }

    /* --- the BASIC grammar AED ships --- */
    {
        static char text[4096];
        int n = 0;
        {
            FILE* f = fopen("config/aed/syntax/bas.cfg", "rb");
            if (f == NULL) {
                fprintf(stderr, "FAIL  cannot open config/aed/syntax/bas.cfg\n");
                failures++;
            } else {
                n = (int) fread(text, 1, sizeof(text), f);
                fclose(f);
            }
        }
        stub_file_reset();
        stub_file_add("/bas.cfg", text, n);
        syn_clear(&g);
        check("the BASIC grammar loads", syn_load(&g, "/bas.cfg") ? 1 : 0, 1);
        check("  and claims a .bas file", syn_covers(&g, "/hello.bas") ? 1 : 0, 1);
        /*
         * And not a .bbc one. That is tokenised BASIC -- the bytes a machine
         * runs, with every keyword replaced by one of them -- so there is no
         * text in it for a grammar to find. Claiming it would colour a binary
         * by the few bytes that happen to spell something.
         */
        check("  and leaves tokenised BASIC alone",
              syn_covers(&g, "/hello.bbc") ? 1 : 0, 0);
        check("  and leaves a .c file alone",
              syn_covers(&g, "/main.c") ? 1 : 0, 0);
        check("  BASIC is case insensitive", g.nocase ? 1 : 0, 1);

        /*
         * add_words drops the rest of a set when either budget runs out, and it
         * does so without a word of complaint -- so the check that matters is
         * not that the file parsed, it is that the last word of the longest set
         * still matches. Breaking it would look like one keyword that quietly
         * stopped colouring.
         */
        check("  its word sets fit", g.nwords < SYN_WORDS_MAX
              && g.noffs < SYN_WORDOFF_MAX ? 1 : 0, 1);
        check_lex("LINE", &g, "KKKK");     /* last of keyword.control */
        check_lex("VPOS", &g, "YYYY");     /* last of support.function */
        check_lex("DIV", &g, "OOO");       /* last of punctuation.operator */

        check_lex("PRINT \"hi\"", &g, "KKKKKTSSSS");
        check_lex("print \"hi\"", &g, "KKKKKTSSSS");
        check_lex("REM a comment", &g, "CCCCCCCCCCCCC");
        check_lex("REMOVE = 1", &g, "TTTTTTTTTN");
        check_lex("X% = &FF", &g, "TTTTTNNN");
        check_lex("A = 10.5", &g, "TTTTNNNN");
        check_lex("10 PRINT", &g, "NNTKKKKK");
        check_lex("IF A AND B THEN", &g, "KKTTTOOOTTTKKKK");
        check_lex("PRINT MID$(A$,1,2)", &g, "KKKKKTYYYYTTTTNTNT");

        /* A doubled quote closes and reopens rather than escaping, which is
         * what BBC BASIC actually does. */
        check_lex("\"a\"\"b\"", &g, "SSSSSS");
    }

    /* --- a scope a theme can colour without knowing the language --- */
    {
        check("support.function is a class of its own",
              syn_class_of("support.function", 16), TOK_TYPE);
        check("  and an unknown head is still plain text",
              syn_class_of("meta.nonsense", 13), TOK_TEXT);
    }

    /* --- a grammar too big to hold is refused, not half read --- */
    {
        /*
         * Found by adding three lines of comment to the BASIC grammar. It was
         * seven bytes under the buffer; the comment pushed it over; the rules
         * past the cut were dropped; and BASIC went on colouring its keywords
         * while leaving its functions and numbers plain. Nothing said so.
         *
         * Half a grammar is worse than none. It reads as a grammar somebody
         * wrote badly rather than a file that did not fit, and the difference
         * is a long time spent looking in the wrong place.
         */
        static char big[8192];
        int at = 0;
        at += sprintf(big + at,
                      "[syntax]\nname = big\nextensions = .big\n[match]\n"
                      "storage.type = words int\n");
        /* Comment lines, to push it past what the loader can hold. */
        while (at < 6000) {
            at += sprintf(big + at, "# padding to make this file too long\n");
        }
        stub_file_reset();
        stub_file_add("/big.cfg", big, at);
        syn_clear(&g);
        check("a grammar longer than the loader can hold is refused",
              syn_load(&g, "/big.cfg") ? 1 : 0, 0);
        check("  and nothing of it is left behind", g.loaded ? 1 : 0, 0);

        /* And one that fits still loads, rules and all. */
        int fits = sprintf(big,
                           "[syntax]\nname = fits\nextensions = .fits\n"
                           "[match]\nstorage.type = words int\n");
        while (fits < 3000) {
            fits += sprintf(big + fits, "# padding that still fits\n");
        }
        stub_file_reset();
        stub_file_add("/fits.cfg", big, fits);
        syn_clear(&g);
        check("one that fits loads", syn_load(&g, "/fits.cfg") ? 1 : 0, 1);
        check_lex("int", &g, "YYY");
    }

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) failed\n", failures);

        return 1;
    }
    fprintf(stderr, "\nall tests passed\n");

    return 0;
}
