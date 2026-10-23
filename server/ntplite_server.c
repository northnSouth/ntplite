#include "ntplite_server.h"
#include "ntplite_wire.h"


static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static void put_ts(uint8_t *p, const ntplite_timestamp_t *t)
{
    put_u32(p, t->sec);
    put_u32(p + 4, t->nsec);
}

static void get_ts(const uint8_t *p, ntplite_timestamp_t *t)
{
    t->sec  = get_u32(p);
    t->nsec = get_u32(p + 4);
}

static int ts_valid(const ntplite_timestamp_t *t)
{
    return t->nsec < ntplite_NSEC_PER_SEC;
}

static ntplite_error_t read_clock(const ntplite_clock_t *c, ntplite_timestamp_t *out)
{
    if (c->now(c->ctx, out) != 0) {
        return ntplite_ERR_CLOCK;
    }
    return ts_valid(out) ? ntplite_OK : ntplite_ERR_BAD_TIMESTAMP;
}

ntplite_error_t ntplite_server_init(ntplite_server_t *srv, const ntplite_clock_t *clock)
{
    if (srv == NULL || clock == NULL || clock->now == NULL) {
        return ntplite_ERR_INVALID_ARG;
    }
    srv->clock = *clock;
    return ntplite_OK;
}

ntplite_error_t ntplite_server_handle(const ntplite_server_t *srv,
                              const uint8_t *req, size_t req_len,
                              uint8_t *resp, size_t resp_cap,
                              size_t *resp_len)
{
    ntplite_timestamp_t t1, t2, t3;
    ntplite_error_t err;

    if (resp_len == NULL) {
        return ntplite_ERR_INVALID_ARG;
    }
    *resp_len = 0;

    if (srv == NULL || srv->clock.now == NULL || req == NULL || resp == NULL) {
        return ntplite_ERR_INVALID_ARG;
    }
    if (resp_cap < ntplite_PACKET_SIZE) {
        return ntplite_ERR_BUFFER_TOO_SMALL;
    }

    err = read_clock(&srv->clock, &t2); /* as early as possible */
    if (err != ntplite_OK) {
        return err;
    }

    if (req_len != ntplite_PACKET_SIZE) {
        return ntplite_ERR_BAD_LENGTH;
    }
    if (req[ntplite_WIRE_OFF_VERSION] != ntplite_WIRE_VERSION) {
        return ntplite_ERR_BAD_VERSION;
    }
    if (req[ntplite_WIRE_OFF_MODE] != ntplite_WIRE_MODE_REQUEST) {
        return ntplite_ERR_BAD_MODE;
    }
    get_ts(req + ntplite_WIRE_OFF_T1, &t1);
    if (!ts_valid(&t1)) {
        return ntplite_ERR_BAD_TIMESTAMP;
    }

    resp[ntplite_WIRE_OFF_VERSION] = (uint8_t)ntplite_WIRE_VERSION;
    resp[ntplite_WIRE_OFF_MODE] = (uint8_t)ntplite_WIRE_MODE_RESPONSE;
    resp[2] = 0;
    resp[3] = 0;
    put_ts(resp + ntplite_WIRE_OFF_T1, &t1);
    put_ts(resp + ntplite_WIRE_OFF_T2, &t2);

    err = read_clock(&srv->clock, &t3); /* as late as possible */
    if (err != ntplite_OK) {
        return err;
    }
    put_ts(resp + ntplite_WIRE_OFF_T3, &t3);

    *resp_len = ntplite_PACKET_SIZE;
    return ntplite_OK;
}
