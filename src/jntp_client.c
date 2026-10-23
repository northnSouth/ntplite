#include "jntp_client.h"
#include "jntp_wire.h"


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

static void put_ts(uint8_t *p, const jntp_timestamp_t *t)
{
    put_u32(p, t->sec);
    put_u32(p + 4, t->nsec);
}

static void get_ts(const uint8_t *p, jntp_timestamp_t *t)
{
    t->sec  = get_u32(p);
    t->nsec = get_u32(p + 4);
}

static int ts_valid(const jntp_timestamp_t *t)
{
    return t->nsec < JNTP_NSEC_PER_SEC;
}

static int ts_equal(const jntp_timestamp_t *a, const jntp_timestamp_t *b)
{
    return a->sec == b->sec && a->nsec == b->nsec;
}

/* Max ~4.29e18, fits int64_t. */
static int64_t ts_to_ns(const jntp_timestamp_t *t)
{
    return (int64_t)t->sec * (int64_t)JNTP_NSEC_PER_SEC + (int64_t)t->nsec;
}

static jntp_error_t read_clock(const jntp_clock_t *c, jntp_timestamp_t *out)
{
    if (c->now(c->ctx, out) != 0) {
        return JNTP_ERR_CLOCK;
    }
    return ts_valid(out) ? JNTP_OK : JNTP_ERR_BAD_TIMESTAMP;
}

jntp_error_t jntp_client_init(jntp_client_t *cli,
                            const jntp_transport_t *transport,
                            const jntp_clock_t *clock,
                            uint32_t max_attempts,
                            uint32_t timeout_ms)
{
    if (cli == NULL || transport == NULL || clock == NULL ||
        transport->send == NULL || transport->recv == NULL ||
        clock->now == NULL || max_attempts == 0) {
        return JNTP_ERR_INVALID_ARG;
    }
    cli->transport    = *transport;
    cli->clock        = *clock;
    cli->max_attempts = max_attempts;
    cli->timeout_ms   = timeout_ms;
    return JNTP_OK;
}

jntp_error_t jntp_client_sync(const jntp_client_t *cli, jntp_result_t *out)
{
    uint8_t tx[JNTP_PACKET_SIZE];
    uint8_t rx[JNTP_PACKET_SIZE];
    size_t rx_len = 0;
    jntp_timestamp_t t1 = {0, 0}, t2, t3, t4 = {0, 0}, echo;
    uint32_t attempt;
    int received = 0;
    jntp_error_t err;
    int64_t n1, n2, n3, n4;

    if (cli == NULL || out == NULL || cli->transport.send == NULL ||
        cli->transport.recv == NULL || cli->clock.now == NULL ||
        cli->max_attempts == 0) {
        return JNTP_ERR_INVALID_ARG;
    }

    for (attempt = 0; attempt < cli->max_attempts && !received; attempt++) {
        size_t i;

        err = read_clock(&cli->clock, &t1); /* fresh t1 for every attempt */
        if (err != JNTP_OK) {
            return err;
        }

        for (i = 0; i < sizeof tx; i++) {
            tx[i] = 0;
        }
        tx[JNTP_WIRE_OFF_VERSION] = (uint8_t)JNTP_WIRE_VERSION;
        tx[JNTP_WIRE_OFF_MODE] = (uint8_t)JNTP_WIRE_MODE_REQUEST;
        put_ts(tx + JNTP_WIRE_OFF_T1, &t1);

        if (cli->transport.send(cli->transport.ctx, tx, sizeof tx) != 0) {
            continue; /* transport error: retry */
        }

        rx_len = 0;
        if (cli->transport.recv(cli->transport.ctx, rx, sizeof rx, &rx_len,
                                cli->timeout_ms) != 0) {
            continue; /* transport error / timeout: retry */
        }

        err = read_clock(&cli->clock, &t4); /* right after receipt */
        if (err != JNTP_OK) {
            return err;
        }
        received = 1;
    }

    if (!received) {
        return JNTP_ERR_TRANSPORT;
    }

    /* Validation failures below are not retried. */
    if (rx_len != JNTP_PACKET_SIZE) {
        return JNTP_ERR_BAD_LENGTH;
    }
    if (rx[JNTP_WIRE_OFF_VERSION] != JNTP_WIRE_VERSION) {
        return JNTP_ERR_BAD_VERSION;
    }
    if (rx[JNTP_WIRE_OFF_MODE] != JNTP_WIRE_MODE_RESPONSE) {
        return JNTP_ERR_BAD_MODE;
    }

    get_ts(rx + JNTP_WIRE_OFF_T1, &echo);
    get_ts(rx + JNTP_WIRE_OFF_T2, &t2);
    get_ts(rx + JNTP_WIRE_OFF_T3, &t3);

    if (!ts_valid(&echo) || !ts_valid(&t2) || !ts_valid(&t3)) {
        return JNTP_ERR_BAD_TIMESTAMP;
    }
    if (!ts_equal(&echo, &t1)) {
        return JNTP_ERR_ORIGIN_MISMATCH;
    }

    n1 = ts_to_ns(&t1);
    n2 = ts_to_ns(&t2);
    n3 = ts_to_ns(&t3);
    n4 = ts_to_ns(&t4);

    out->delay_ns = (n4 - n1) - (n3 - n2);
    if (out->delay_ns < 0) {
        return JNTP_ERR_BAD_TIMING;
    }
    out->offset_ns = ((n2 - n1) + (n3 - n4)) / 2;
    out->t1 = t1;
    out->t2 = t2;
    out->t3 = t3;
    out->t4 = t4;
    return JNTP_OK;
}
