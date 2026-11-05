#include "ntplite_server.h"
#include "ntplite_internal.h"

#include <string.h>

ntplite_result_t ntplite_server_init(ntplite_server_ctx_t     *ctx,
                                     ntplite_server_platform_t platform)
{
    if (!ctx || !platform.time_fn || !platform.send_fn)
        return NTPLITE_ERR_INVALID;

    memset(ctx, 0, sizeof(*ctx));
    ctx->platform = platform;
    return NTPLITE_OK;
}

ntplite_result_t ntplite_server_on_request(ntplite_server_ctx_t *ctx,
                                           const uint8_t *req, size_t req_len)
{
    if (!ctx || !req) return NTPLITE_ERR_INVALID;

    const ntplite_server_platform_t *p = &ctx->platform;

    ntplite_time_t t2 = p->time_fn(p->user_arg);   /* first thing */

    ntplite_packet_t pkt;
    ntplite_result_t rc = ntplite_packet_decode(req, req_len, &pkt);
    if (rc != NTPLITE_OK) return rc;

    ntplite_time_t t3 = p->time_fn(p->user_arg);   /* last thing  */
    if (!ntplite_is_valid(t2) || !ntplite_is_valid(t3))
        return NTPLITE_ERR_INVALID;

    pkt.t2_sec = (uint32_t)t2.sec;  pkt.t2_nsec = t2.nsec;
    pkt.t3_sec = (uint32_t)t3.sec;  pkt.t3_nsec = t3.nsec;

    uint8_t out[NTPLITE_PACKET_SIZE];
    rc = ntplite_packet_encode(&pkt, out, sizeof(out));
    if (rc != NTPLITE_OK) return rc;

    if (p->send_fn(out, sizeof(out), p->user_arg) != NTPLITE_OK)
        return NTPLITE_ERR_TRANSPORT;
    return NTPLITE_OK;
}
