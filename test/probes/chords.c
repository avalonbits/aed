/*
 * chords -- report what a key chord actually delivers to the editor.
 *
 * The question this answers: when CTRL+P does nothing, is the command wrong or
 * did the key never arrive? AED decides what a key means from `vkey` and
 * `mods`, so this prints both, for every key, through the same reader the
 * editor uses -- keys_wait() over the event queue, not getch().
 *
 * Each line is:
 *
 *   n  ch=<code> vkey=<code> mods=<C|S|A>  <what ctrlCmds would make of it>
 *
 * Press the chord in question. If nothing prints at all, the key never reached
 * the program and no amount of editor code will help. If a line prints with
 * mods missing C, the modifier was not delivered with it. If it prints with
 * vkey 0, MOS sent a character but no key code.
 *
 * Output goes to /chords.out as well as the screen, because a headless
 * emulator shows nothing.
 *
 * ESC on its own quits.
 */
#include <stdio.h>
#include <string.h>

#include <agon/mos.h>

#include "keys.h"
#include "vkey.h"

static char log_;

static void say(const char* s) {
    printf("%s\r\n", s);
    if (log_) {
        mos_fwrite(log_, (char*) s, (unsigned) strlen(s));
        mos_fwrite(log_, "\r\n", 2);
    }
}

static int put_n(char* o, int v) {
    char t[8];
    int n = 0;

    if (v == 0) {
        t[n++] = '0';
    }
    while (v > 0) {
        t[n++] = (char) ('0' + (v % 10));
        v /= 10;
    }
    int k = 0;
    while (n > 0) {
        o[k++] = t[--n];
    }

    return k;
}

// What CTRL makes of this key, named rather than as a pointer, so the line
// says something a person can act on.
static const char* means(VKey vkey) {
    switch (vkey) {
        case VK_p: case VK_P: return "CTRL+P  find previous";
        case VK_n: case VK_N: return "CTRL+N  find next";
        case VK_f: case VK_F: return "CTRL+F  find";
        case VK_g: case VK_G: return "CTRL+G  go to line";
        case VK_HOME: case VK_KP_HOME: return "CTRL+HOME  top of file";
        case VK_END: case VK_KP_END: return "CTRL+END  end of file";
        default: return "";
    }
}

int main(void) {
    log_ = mos_fopen("/chords.out", FA_WRITE | FA_CREATE_ALWAYS);
    say("chords: press a chord. ESC quits.");
    say("n  ch vkey mods  meaning");

    keys_open();

    static char line[96];
    int n = 0;
    for (;;) {
        const key_press kp = keys_wait();
        if (kp.vkey == VK_ESCAPE && kp.mods == 0) {
            break;
        }
        n++;

        int k = 0;
        k += put_n(line + k, n);
        line[k++] = ' ';
        line[k++] = 'c'; line[k++] = 'h'; line[k++] = '=';
        k += put_n(line + k, (int) (unsigned char) kp.ch);
        line[k++] = ' ';
        line[k++] = 'v'; line[k++] = 'k'; line[k++] = '=';
        k += put_n(line + k, (int) kp.vkey);
        line[k++] = ' ';
        line[k++] = 'm'; line[k++] = 'o'; line[k++] = 'd'; line[k++] = '=';
        line[k++] = (kp.mods & MOD_CTRL) ? 'C' : '-';
        line[k++] = (kp.mods & MOD_SHFT) ? 'S' : '-';
        line[k++] = (kp.mods & MOD_ALT)  ? 'A' : '-';
        line[k++] = ' ';
        line[k++] = ' ';
        const char* m = means(kp.vkey);
        const int msz = (int) strlen(m);
        memcpy(line + k, m, (size_t) msz);
        k += msz;
        line[k] = 0;
        say(line);
    }

    say("done");
    if (log_) {
        mos_fclose(log_);
    }

    return 0;
}
