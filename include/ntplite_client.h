#ifndef NTPLITE_CLIENT_H
#define NTPLITE_CLIENT_H

#include "ntplite_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ntplite_time_fn time_fn;
    ntplite_send_fn send_fn;
    ntplite_recv_fn recv_fn;
    void           *user_arg;
} ntplite_client_platform_t;

/*
 * slew_shift     : max slew rate is 2^-slew_shift, valid 1..30
 *                  (11 -> ~488 ppm). Keeps time monotonic, no 64-bit math.
 * step_threshold : step instead of slewing when the error is at least this.
 * max_delta      : exchanges with a round-trip delay above this are
 *                  discarded (NTPLITE_ERR_DELAY). Zero disables the check.
 */
typedef struct {
    uint8_t            slew_shift;
    ntplite_duration_t step_threshold;
    ntplite_duration_t max_delta;
} ntplite_slew_cfg_t;

/*
 * One client per context, no internal locking. ntplite_now() and
 * ntplite_client_sync() must not run concurrently or preempt each other
 * (same thread, or serialized by the caller).
 */
typedef struct {
    ntplite_client_platform_t platform;
    ntplite_slew_cfg_t        slew_cfg;

    ntplite_duration_t theta;     /* target offset                    */
    ntplite_duration_t applied;   /* offset currently added to raw    */
    ntplite_time_t     last_raw;  /* raw time of last slew progress   */
    bool               synced;

    uint8_t            buf[NTPLITE_PACKET_SIZE];  /* shared tx/rx     */

    /* Diagnostics only: never used for decisions, zeroed by init. */
    struct {
        uint32_t syncs_ok;
        uint32_t steps;            /* includes the first sync              */
        uint32_t stale_dropped;    /* replies whose t1 did not match ours  */
        uint32_t err_transport;
        uint32_t err_mismatch;
        uint32_t err_delay;
        uint32_t err_negative_rtt;
        uint32_t err_invalid;      /* bad clock reading or reply timestamp */
    } debug;
} ntplite_client_ctx_t;

ntplite_result_t ntplite_client_init(ntplite_client_ctx_t     *ctx,
                                     ntplite_client_platform_t platform,
                                     ntplite_slew_cfg_t        slew_cfg);

/**
 * One blocking exchange: send_fn once, then recv_fn up to
 * NTPLITE_MAX_RECV_ATTEMPTS times, discarding stale replies from earlier
 * timed-out exchanges. On any error the sync state (theta, applied, last_raw,
 * synced) is kept; only the debug counters change.
 */
ntplite_result_t ntplite_client_sync(ntplite_client_ctx_t *ctx);

/**
 * Synchronized time. Calls time_fn only. Returns the raw clock until the first
 * successful sync, and also if time_fn returns an invalid value (state untouched).
 */
ntplite_time_t ntplite_now(ntplite_client_ctx_t *ctx);

bool ntplite_client_is_synced(const ntplite_client_ctx_t *ctx);

#ifdef __cplusplus
}
#endif
#endif /* NTPLITE_CLIENT_H */