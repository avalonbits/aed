/*
 * hub/hub.h -- the hub client API, for C programs built with agondev or acc.
 *
 * hub is a resident shell for MOS 3.0.2. A program running under it can ask
 * hub to run commands after it returns, and to bring it back afterwards:
 *
 *     if (hub_present()) {
 *         struct view *v = hub_block("FMGR", sizeof *v);   // survives the run
 *
 *         save_view(v);
 *         hub_enter("FMGR");
 *         hub_push("aed notes.txt", 0);
 *         hub_return_to("fmgr -resume");
 *
 *         return 0;                                        // hub takes over
 *     }
 *
 * Nothing runs until the program returns: the calls only record what to do.
 * Call hub_present() first; every other call fails (returns HUB_ERR_ABSENT,
 * or 0 for hub_block) when it hasn't found hub. A program must keep working
 * without hub -- on a machine without it, hub_present() is simply false.
 *
 * Link with libhub.a: lib/agondev/libhub.a or lib/acc/libhub.a, both
 * assembled by zap from lib/hub_glue.s.
 */
#ifndef HUB_H
#define HUB_H

#include <stdbool.h>
#include <stddef.h>

/* Flags for hub_push. */
#define HUB_STOP_ON_ERROR   0x01    /* a non-zero result skips the rest of the
                                       frame's jobs, up to its continuation */
#define HUB_USER_PROGRAM    0x02    /* start with the screen as hub's prompt
                                       had it: its mode (cleared), colours,
                                       viewports, its font, the cursor shown;
                                       and afterwards (after any pause) put it
                                       back so, for what runs next. Needs hub
                                       0.3; the font, 0.4.1. */
#define HUB_PAUSE_AFTER     0x04    /* afterwards, worked or not: "Press a key
                                       to return", and wait for one -- unless
                                       the variable Hub$NoPause is set, as for
                                       tests. Needs hub 0.3. */

#define HUB_CMD_MAX         93      /* longest command a job can hold */
#define HUB_MAX_JOBS        8       /* jobs waiting, continuations included;
                                       the running job doesn't count */

/* VDP buffers 0x4800-0x48FF are hub's; programs should leave them alone. */
#define HUB_SCREEN_BUFFER   0x4855  /* the last user program's screen */
#define HUB_RESULT_RESET    255     /* the result of a job a reset cut short */

/* Status codes. */
#define HUB_OK              0
#define HUB_ERR_DEPTH       1       /* hub_enter: frames nested too deep */
#define HUB_ERR_NO_FRAME    1       /* hub_push, hub_return_to: no hub_enter */
#define HUB_ERR_FULL        2       /* the queue is full */
#define HUB_ERR_TOO_LONG    3       /* the command is longer than HUB_CMD_MAX */
#define HUB_ERR_ABSENT      255     /* hub_present() hasn't found hub */

/* True if hub is running and speaks a version this header knows. */
bool hub_present(void);

/* Open a frame for the jobs pushed next, named by a 4-character tag. */
int hub_enter(const char tag[4]);

/* Queue a command, as it would be typed at the prompt, in the open frame. */
int hub_push(const char *cmd, unsigned char flags);

/* Set the frame's continuation: run last, even if a job before it failed. */
int hub_return_to(const char *cmd);

/* In a continuation: the result of the last job of the frame it closes, and
 * the index (in push order) of the job that stopped that frame, or -1.
 * Anywhere else -- a program started afresh -- 0 and -1. */
int hub_last_result(void);
int hub_failed_job(void);

/* Memory that keeps its contents between runs, zeroed when first created.
 * NULL if there is no room, or the tag exists with a smaller size. */
void *hub_block(const char tag[4], size_t size);

/* How many frames are open. */
int hub_depth(void);

/* In a continuation: 1 if the machine was reset while its frame's job was
 * running -- that job then counts as failed, with HUB_RESULT_RESET -- else
 * 0. Anywhere else: 0. Needs hub 0.2 or later. */
int hub_resumed(void);

/* The screen mode the last HUB_USER_PROGRAM job left the screen in, or -1 if
 * there is none (or hub is older than 0.4). hub captured that screen, after
 * the program and before any pause, into the VDP buffer HUB_SCREEN_BUFFER as
 * a bitmap. To show it again: switch to that mode if it differs, then
 *
 *     VDU 23,27,&20,HUB_SCREEN_BUFFER;    select it
 *     VDU 23,27,3,0;0;                    draw it at the top left
 *
 * The capture needs VDP 2.2.0 or later; on an older VDP the buffer is empty
 * and drawing it does nothing. */
int hub_user_screen(void);

#endif
