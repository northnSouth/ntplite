#ifndef NTPLITE_CLIENT_H
#define NTPLITE_CLIENT_H

#include "ntplite_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The client only reads the clock; the caller moves the bytes. */
typedef struct {
    ntplite_time_fn time_fn;    /* raw clock: the same clock t1 and t4 must come from */
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
 * One client per context, no internal locking: every call on a context, ntplite_now() included,
 * must come from one thread (or be serialized by the caller). Typical firmware: the receive ISR
 * stamps t4 and queues (payload, t4) to the timekeeper task, which calls ntplite_client_on_reply().
 */
typedef struct {
    ntplite_client_platform_t platform;
    ntplite_slew_cfg_t        slew_cfg;

    ntplite_duration_t theta;     /* target offset                    */
    ntplite_duration_t applied;   /* offset currently added to raw    */
    ntplite_time_t     last_raw;  /* raw time of last slew progress   */
    bool               synced;

    uint8_t            buf[NTPLITE_PACKET_SIZE];  /* last request     */
    ntplite_time_t     req_t1;    /* t1 of the last request           */
    bool               req_made;  /* at least one request was built   */
    bool               pending;   /* a reply to req_t1 is awaited     */

    /* Diagnostics only: never used for decisions, zeroed by init. */
    struct {
        uint32_t syncs_ok;
        uint32_t steps;            /* includes the first sync                       */
        uint32_t stale_dropped;    /* replies that do not answer the request in flight */
        uint32_t aborted;          /* requests given up with ntplite_client_abort()  */
        uint32_t err_delay;
        uint32_t err_negative_rtt;
        uint32_t err_invalid;      /* bad t1/t4, short reply, out-of-range fields    */
    } debug;
} ntplite_client_ctx_t;

ntplite_result_t ntplite_client_init(ntplite_client_ctx_t     *ctx,
                                     ntplite_client_platform_t platform,
                                     ntplite_slew_cfg_t        slew_cfg);

/**
 * Starts an exchange: writes a request carrying t1 into the context (see ntplite_client_tx()).
 * t1 is the raw clock (same clock as time_fn), stamped as close to the actual transmission as
 * possible: any gap before the bytes leave counts as forward-path delay and biases the offset.
 * A request that is still pending is replaced. t1 must differ from the previous request's t1, or
 * a late reply to that one would be taken for this one (NTPLITE_ERR_INVALID).
 */
ntplite_result_t ntplite_client_begin(ntplite_client_ctx_t *ctx, ntplite_time_t t1);

/** The request to send: valid until the next ntplite_client_begin(). {NULL, 0} before the first. */
ntplite_span_t ntplite_client_tx(const ntplite_client_ctx_t *ctx);

/**
 * Handles one received reply payload. t4 is the raw clock at arrival: stamp it where the bytes
 * arrive (ISR / DMA callback), not where this function happens to run.
 *
 *   NTPLITE_OK            sample applied (stepped or slewing)
 *   NTPLITE_ERR_MISMATCH  not an answer to the request in flight (late reply to an aborted
 *                         request, duplicate, or nothing pending): ignored, still pending
 *   NTPLITE_ERR_BUFFER    shorter than a packet: ignored, still pending
 *   other errors          it was our reply but unusable: exchange over, sync state unchanged
 */
ntplite_result_t ntplite_client_on_reply(ntplite_client_ctx_t *ctx,
                                         const uint8_t *reply, size_t len,
                                         ntplite_time_t t4);

/** Whether a reply is awaited. */
bool ntplite_client_pending(const ntplite_client_ctx_t *ctx);

/** Gives up on the request in flight (e.g. on timeout); a late reply to it will be stale. */
void ntplite_client_abort(ntplite_client_ctx_t *ctx);

/**
 * Synchronized time. Returns the raw clock until the first successful
 * sync, and also if time_fn returns an invalid value (state untouched).
 */
ntplite_time_t ntplite_now(ntplite_client_ctx_t *ctx);

bool ntplite_client_is_synced(const ntplite_client_ctx_t *ctx);

#ifdef __cplusplus
}
#endif
#endif /* NTPLITE_CLIENT_H */