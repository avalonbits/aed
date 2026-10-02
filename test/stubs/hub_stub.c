/*
 * hub, for the host tests: present or not as a test says, a block that keeps
 * what is written to it, and a record of what was queued -- the commands and
 * their flags, and the continuation -- for the test to read back. What the
 * last frame left, a failed job and a result, is set by the test as though a
 * run had happened.
 */

#include <string.h>

#include <hub/hub.h>

#include "hub_stub.h"

static bool present;
// Whether this program has asked hub_present yet: until it has, every other
// call fails, as the real one's do -- the API is found through it.
static bool found;
static int failed = -1;
static int result;
static unsigned char store[1024];
static size_t stored;
static char tag[4];

static int njobs;
static char jobs[HUB_MAX_JOBS][HUB_CMD_MAX + 1];
static unsigned char flags[HUB_MAX_JOBS];
static char cont[HUB_CMD_MAX + 1];
static bool entered;

void stub_hub_reset(bool is_present) {
    present = is_present;
    found = false;
    failed = -1;
    result = 0;
    memset(store, 0, sizeof(store));
    stored = 0;
    memset(tag, 0, sizeof(tag));
    njobs = 0;
    cont[0] = 0;
    entered = false;
}

void stub_hub_ran(int failed_job, int last_result) {
    found = false;                      // a new program: hub not found yet
    failed = failed_job;
    result = last_result;
    njobs = 0;
    cont[0] = 0;
    entered = false;
}

int stub_hub_jobs(void) { return njobs; }
const char* stub_hub_job(int i) { return i >= 0 && i < njobs ? jobs[i] : ""; }
int stub_hub_flags(int i) { return i >= 0 && i < njobs ? flags[i] : -1; }
const char* stub_hub_continuation(void) { return cont; }

bool hub_present(void) {
    found = present;

    return present;
}

int hub_enter(const char t[4]) {
    if (!found) {
        return HUB_ERR_ABSENT;
    }
    (void) t;
    entered = true;

    return HUB_OK;
}

int hub_push(const char* cmd, unsigned char f) {
    if (!found) {
        return HUB_ERR_ABSENT;
    }
    if (!entered) {
        return HUB_ERR_NO_FRAME;
    }
    if (njobs >= HUB_MAX_JOBS) {
        return HUB_ERR_FULL;
    }
    if (strlen(cmd) > HUB_CMD_MAX) {
        return HUB_ERR_TOO_LONG;
    }
    strcpy(jobs[njobs], cmd);
    flags[njobs] = f;
    njobs++;

    return HUB_OK;
}

int hub_return_to(const char* cmd) {
    if (!found) {
        return HUB_ERR_ABSENT;
    }
    if (!entered) {
        return HUB_ERR_NO_FRAME;
    }
    if (strlen(cmd) > HUB_CMD_MAX) {
        return HUB_ERR_TOO_LONG;
    }
    strcpy(cont, cmd);

    return HUB_OK;
}

// Not found, the real calls answer 0 -- and a failed job of 0 reads as the
// first job having failed, which is the trap a program has to avoid.
int hub_last_result(void) { return found ? result : 0; }
int hub_failed_job(void) { return found ? failed : 0; }

void* hub_block(const char t[4], size_t size) {
    if (!found || size > sizeof(store)) {
        return NULL;
    }
    if (stored == 0) {
        memcpy(tag, t, 4);
        stored = size;
    } else if (memcmp(tag, t, 4) != 0 || size > stored) {
        return NULL;
    }

    return store;
}

int hub_depth(void) { return entered ? 1 : 0; }
int hub_resumed(void) { return 0; }
int hub_user_screen(void) { return -1; }
