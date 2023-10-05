#include "input_handler.h"

#include <mos_api.h>


key_command read_input() {
    char ch = getch();

    key_command kc = {CMD_PUTC, ch};
    return kc;
}

