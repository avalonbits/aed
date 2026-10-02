/*
 * The host tests' hub: see hub_stub.c.
 */
#ifndef HUB_STUB_H
#define HUB_STUB_H

#include <stdbool.h>

// Starts again: hub running or not, nothing queued, the block empty.
void stub_hub_reset(bool present);

// As though the queued run happened: the job that stopped the frame (-1 for
// none) and the last job's result. The queue is emptied; the block is kept.
// What runs next is a new program, so hub is not found until it asks.
void stub_hub_ran(int failed_job, int last_result);

// What CTRL+R queued: the jobs in order with their flags, and the
// continuation.
int stub_hub_jobs(void);
const char* stub_hub_job(int i);
int stub_hub_flags(int i);
const char* stub_hub_continuation(void);

#endif
