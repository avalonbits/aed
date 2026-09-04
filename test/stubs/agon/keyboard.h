/* Host-test stub for <agon/keyboard.h>, the MOS key event queue.
 *
 * The real one fills a ring buffer from a MOS key vector. This one serves the
 * sequence a test scripted with stub_set_keys, so the whole input path --
 * keys_wait's filtering, read_input's translation, the modal prompt loops --
 * runs against the same events a keyboard would produce. */
#ifndef _TEST_STUB_AGON_KEYBOARD_H_
#define _TEST_STUB_AGON_KEYBOARD_H_

#include <stdbool.h>
#include <stdint.h>

struct keyboard_event_t {
    uint8_t ascii;
    uint8_t kmod;
    uint8_t vkey;
    uint8_t isdown;
};

void kbuf_init(uint8_t buf_len);
bool kbuf_poll_event(struct keyboard_event_t* e);
void kbuf_clear(void);
void kbuf_deinit(void);

#endif
