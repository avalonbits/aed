#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <agon/keyboard.h>
#include <agon/vdp.h>

/* Does the MOS key event vector fire for CTRL+SHIFT+<arrow>, and does the
 * event carry the modifiers? Prints every event as it arrives.
 * ESC (vkey 27 down, no modifiers) quits. */
int main(void) {
    struct keyboard_event_t e;

    vdp_cls();
    printf("kbuf events -- ESC alone quits.\r\n");
    printf("ascii kmod vkey down\r\n");

    kbuf_init(32);

    for (;;) {
        if (!kbuf_poll_event(&e)) {
            continue;
        }
        printf("%03u   %03u  %03u  %u\r\n",
               (unsigned) e.ascii, (unsigned) e.kmod,
               (unsigned) e.vkey, (unsigned) e.isdown);
        if (e.ascii == 27 && e.kmod == 0 && e.isdown) {
            break;
        }
    }

    kbuf_deinit();

    return 0;
}
