#ifndef JNTP_CLIENT_H
#define JNTP_CLIENT_H

#include "jntp_types.h"

/* send/recv return 0 on success, nonzero on transport error (incl. timeout). */
typedef struct {
    int (*send)(void *ctx, const uint8_t *buf, size_t len);
    int (*recv)(void *ctx, uint8_t *buf, size_t cap, size_t *len,
                uint32_t timeout_ms);
    void *ctx;
} jntp_transport_t;

typedef struct {
    jntp_transport_t transport;
    jntp_clock_t     clock;
    uint32_t        max_attempts; /* >= 1, total tries on transport error */
    uint32_t        timeout_ms;   /* passed to transport.recv */
} jntp_client_t;

typedef struct {
    jntp_timestamp_t t1; /* client tx */
    jntp_timestamp_t t2; /* server rx */
    jntp_timestamp_t t3; /* server tx */
    jntp_timestamp_t t4; /* client rx */
    int64_t offset_ns;  /* server - client; positive = client is behind */
    int64_t delay_ns;   /* round-trip network delay */
} jntp_result_t;

jntp_error_t jntp_client_init(jntp_client_t *cli,
                            const jntp_transport_t *transport,
                            const jntp_clock_t *clock,
                            uint32_t max_attempts,
                            uint32_t timeout_ms);

/* One synchronisation exchange. Caller decides when to call it. */
jntp_error_t jntp_client_sync(const jntp_client_t *cli, jntp_result_t *out);

#endif /* JNTP_CLIENT_H */
