#include "ntplite_client.h"
#include "ntplite_internal.h"

#include <string.h>

/* Wire seconds are unsigned; reject anything that doesn't fit int32_t. */
static bool from_wire(uint32_t sec, uint32_t nsec, ntplite_time_t *out)
{
    if (sec > (uint32_t)INT32_MAX || nsec >= NTPLITE_NSEC_PER_SEC)
        return false;
    out->sec  = (int32_t)sec;
    out->nsec = nsec;
    return true;
}

static bool same_time(ntplite_time_t a, ntplite_time_t b)
{
    return a.sec == b.sec && a.nsec == b.nsec;
}

/*
 * Move `applied` toward `theta` by at most (raw - last_raw) * 2^-shift.
 * Shared by sync and now, so slew progress is never discarded and the
 * step decision in sync sees an up-to-date `applied`.
 */
static void advance(ntplite_client_ctx_t *ctx, ntplite_time_t raw)
{
    ntplite_duration_t err  = ntplite_sub(ctx->theta, ctx->applied);
    int                sign = ntplite_sign(err);

    if (sign == 0) {
        ctx->last_raw = raw;        /* nothing to slew; keep reference fresh */
        return;
    }

    ntplite_duration_t elapsed = ntplite_sub(raw, ctx->last_raw);
    if (ntplite_sign(elapsed) < 0) {
        ctx->last_raw = raw;        /* raw clock went backwards */
        return;
    }

    ntplite_duration_t budget = ntplite_shr(elapsed, ctx->slew_cfg.slew_shift);

    /* Zero budget: let elapsed time keep accumulating. */
    if (ntplite_sign(budget) > 0) {
        if (ntplite_cmp(ntplite_abs(err), budget) > 0)
            err = (sign > 0) ? budget : ntplite_neg(budget);
        ctx->applied  = ntplite_add(ctx->applied, err);
        ctx->last_raw = raw;
    }
}

ntplite_result_t ntplite_client_init(ntplite_client_ctx_t     *ctx,
                                     ntplite_client_platform_t platform,
                                     ntplite_slew_cfg_t        slew_cfg)
{
    if (!ctx || !platform.time_fn)
        return NTPLITE_ERR_INVALID;
    if (slew_cfg.slew_shift < 1 || slew_cfg.slew_shift > 30)
        return NTPLITE_ERR_INVALID;
    if (!ntplite_is_valid(slew_cfg.step_threshold) ||
        ntplite_sign(slew_cfg.step_threshold) <= 0)
        return NTPLITE_ERR_INVALID;
    if (!ntplite_is_valid(slew_cfg.max_delta))
        return NTPLITE_ERR_INVALID;

    memset(ctx, 0, sizeof(*ctx));
    ctx->platform = platform;
    ctx->slew_cfg = slew_cfg;
    return NTPLITE_OK;
}

ntplite_result_t ntplite_client_begin(ntplite_client_ctx_t *ctx, ntplite_time_t t1)
{
    ntplite_packet_t pkt;

    if (!ctx) return NTPLITE_ERR_INVALID;

    /* Replies are matched by their echoed t1: a repeated t1 would let a late reply to the
     * previous request pass for this one (coarse clocks, two requests within one tick). */
    if (!ntplite_is_valid(t1) || (ctx->req_made && same_time(t1, ctx->req_t1))) {
        ctx->debug.err_invalid++;
        return NTPLITE_ERR_INVALID;
    }

    memset(&pkt, 0, sizeof(pkt));
    pkt.t1_sec  = (uint32_t)t1.sec;
    pkt.t1_nsec = t1.nsec;
    if (ntplite_packet_encode(&pkt, ctx->buf, sizeof(ctx->buf)) != NTPLITE_OK)
        return NTPLITE_ERR_BUFFER;      /* unreachable: buf is exactly one packet */

    ctx->req_t1   = t1;
    ctx->req_made = true;
    ctx->pending  = true;
    return NTPLITE_OK;
}

ntplite_span_t ntplite_client_tx(const ntplite_client_ctx_t *ctx)
{
    ntplite_span_t s = { NULL, 0 };

    if (ctx && ctx->req_made) {
        s.data = ctx->buf;
        s.len  = NTPLITE_PACKET_SIZE;
    }
    return s;
}

bool ntplite_client_pending(const ntplite_client_ctx_t *ctx)
{
    return ctx && ctx->pending;
}

void ntplite_client_abort(ntplite_client_ctx_t *ctx)
{
    if (ctx && ctx->pending) {
        ctx->pending = false;
        ctx->debug.aborted++;
    }
}

/* Everything that can fail happens before the first write to sync state. */
static ntplite_result_t apply_reply(ntplite_client_ctx_t *ctx, const uint8_t *reply,
                                    size_t len, ntplite_time_t t4)
{
    ntplite_packet_t   pkt;
    ntplite_time_t     t2, t3;
    ntplite_duration_t delta, theta, err;
    ntplite_result_t   rc;

    if (!reply) return NTPLITE_ERR_INVALID;

    rc = ntplite_packet_decode(reply, len, &pkt);
    if (rc != NTPLITE_OK) return rc;    /* can't tell whose it is: still pending */

    if (!ctx->pending ||
        pkt.t1_sec != (uint32_t)ctx->req_t1.sec || pkt.t1_nsec != ctx->req_t1.nsec)
        return NTPLITE_ERR_MISMATCH;

    ctx->pending = false;               /* our reply, whatever it says */

    if (!ntplite_is_valid(t4)) return NTPLITE_ERR_INVALID;
    if (!from_wire(pkt.t2_sec, pkt.t2_nsec, &t2) ||
        !from_wire(pkt.t3_sec, pkt.t3_nsec, &t3))
        return NTPLITE_ERR_INVALID;

    rc = ntplite_offset(ctx->req_t1, t2, t3, t4, &delta, &theta);
    if (rc != NTPLITE_OK) return rc;

    /* Drop high-delay samples: their offset estimate is unreliable. */
    if (ntplite_sign(ctx->slew_cfg.max_delta) > 0 &&
        ntplite_cmp(delta, ctx->slew_cfg.max_delta) > 0)
        return NTPLITE_ERR_DELAY;

    /* Commit. Drain toward the OLD target first, so the step check below
     * sees the offset that is really in effect at t4. */
    if (ctx->synced)
        advance(ctx, t4);

    ctx->theta = theta;

    /* First sync, or error too large to slew: step. */
    err = ntplite_abs(ntplite_sub(theta, ctx->applied));
    if (!ctx->synced ||
        ntplite_cmp(err, ctx->slew_cfg.step_threshold) >= 0) {
        ctx->applied = theta;
        ctx->debug.steps++;
    }

    ctx->last_raw = t4;
    ctx->synced   = true;
    return NTPLITE_OK;
}

ntplite_result_t ntplite_client_on_reply(ntplite_client_ctx_t *ctx,
                                         const uint8_t *reply, size_t len,
                                         ntplite_time_t t4)
{
    ntplite_result_t rc;

    if (!ctx) return NTPLITE_ERR_INVALID;

    rc = apply_reply(ctx, reply, len, t4);
    switch (rc) {
    case NTPLITE_OK:               ctx->debug.syncs_ok++;         break;
    case NTPLITE_ERR_MISMATCH:     ctx->debug.stale_dropped++;    break;
    case NTPLITE_ERR_DELAY:        ctx->debug.err_delay++;        break;
    case NTPLITE_ERR_NEGATIVE_RTT: ctx->debug.err_negative_rtt++; break;
    default:                       ctx->debug.err_invalid++;      break;
    }
    return rc;
}

ntplite_time_t ntplite_now(ntplite_client_ctx_t *ctx)
{
    ntplite_time_t raw = { 0, 0 };
    if (!ctx) return raw;

    raw = ctx->platform.time_fn(ctx->platform.user_arg);
    if (!ntplite_is_valid(raw) || !ctx->synced) return raw;

    advance(ctx, raw);
    return ntplite_add(raw, ctx->applied);
}

bool ntplite_client_is_synced(const ntplite_client_ctx_t *ctx)
{
    return ctx && ctx->synced;
}