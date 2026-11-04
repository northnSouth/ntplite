#include "ntplite_client.h"
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
    if (!ctx || !platform.time_fn || !platform.send_fn || !platform.recv_fn)
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

ntplite_result_t ntplite_client_sync(ntplite_client_ctx_t *ctx)
{
    if (!ctx) return NTPLITE_ERR_INVALID;

    const ntplite_client_platform_t *p = &ctx->platform;
    ntplite_result_t rc;

    ntplite_time_t t1 = p->time_fn(p->user_arg);
    if (!ntplite_is_valid(t1)) return NTPLITE_ERR_INVALID;

    ntplite_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.t1_sec  = (uint32_t)t1.sec;
    pkt.t1_nsec = t1.nsec;

    rc = ntplite_packet_encode(&pkt, ctx->buf, sizeof(ctx->buf));
    if (rc != NTPLITE_OK) return rc;
    rc = p->send_fn(ctx->buf, NTPLITE_PACKET_SIZE, p->user_arg);
    if (rc != NTPLITE_OK) return rc;

    /* Read until a reply echoes our t1; discard stale ones. */
    ntplite_time_t t4 = { 0, 0 };
    bool matched = false;
    for (size_t i = 0; i < NTPLITE_MAX_RECV_ATTEMPTS; i++) {
        rc = p->recv_fn(ctx->buf, NTPLITE_PACKET_SIZE, p->user_arg);
        if (rc != NTPLITE_OK) return rc;

        t4 = p->time_fn(p->user_arg);   /* right after this recv */

        rc = ntplite_packet_decode(ctx->buf, sizeof(ctx->buf), &pkt);
        if (rc != NTPLITE_OK) return rc;

        if (pkt.t1_sec == (uint32_t)t1.sec && pkt.t1_nsec == t1.nsec) {
            matched = true;
            break;
        }
    }
    if (!matched)              return NTPLITE_ERR_MISMATCH;
    if (!ntplite_is_valid(t4)) return NTPLITE_ERR_INVALID;

    ntplite_time_t t2, t3;
    if (!from_wire(pkt.t2_sec, pkt.t2_nsec, &t2) ||
        !from_wire(pkt.t3_sec, pkt.t3_nsec, &t3))
        return NTPLITE_ERR_INVALID;

    ntplite_duration_t delta, theta;
    rc = ntplite_offset(t1, t2, t3, t4, &delta, &theta);
    if (rc != NTPLITE_OK) return rc;

    /* Drop high-delay samples: their offset estimate is unreliable. */
    if (ntplite_sign(ctx->slew_cfg.max_delta) > 0 &&
        ntplite_cmp(delta, ctx->slew_cfg.max_delta) > 0)
        return NTPLITE_ERR_DELAY;

    /* Drain toward the OLD target first, so the step check below sees
     * the offset that is really in effect at t4. */
    if (ctx->synced)
        advance(ctx, t4);

    ctx->theta = theta;

    /* First sync, or error too large to slew: step. */
    ntplite_duration_t err = ntplite_abs(ntplite_sub(theta, ctx->applied));
    if (!ctx->synced ||
        ntplite_cmp(err, ctx->slew_cfg.step_threshold) >= 0)
        ctx->applied = theta;

    ctx->last_raw = t4;
    ctx->synced   = true;
    return NTPLITE_OK;
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
