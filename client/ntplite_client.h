#ifndef ntplite_CLIENT_H
#define ntplite_CLIENT_H

#include "ntplite_types.h"

/* send/recv return 0 on success, nonzero on transport error (incl. timeout). */
typedef struct {
    int (*send)(void *ctx, const uint8_t *buf, size_t len);
    int (*recv)(void *ctx, uint8_t *buf, size_t cap, size_t *len,
                uint32_t timeout_ms);
    void *ctx;
} ntplite_transport_t;

typedef struct {
    ntplite_transport_t transport;
    ntplite_clock_t     clock;
    uint32_t        max_attempts; /* >= 1, total tries on transport error */
    uint32_t        timeout_ms;   /* passed to transport.recv */
} ntplite_client_t;

typedef struct {
    ntplite_timestamp_t t1; /* client tx */
    ntplite_timestamp_t t2; /* server rx */
    ntplite_timestamp_t t3; /* server tx */
    ntplite_timestamp_t t4; /* client rx */
    int64_t offset_ns;  /* server - client; positive = client is behind */
    int64_t delay_ns;   /* round-trip network delay */
} ntplite_result_t;

ntplite_error_t ntplite_client_init(ntplite_client_t *cli,
                            const ntplite_transport_t *transport,
                            const ntplite_clock_t *clock,
                            uint32_t max_attempts,
                            uint32_t timeout_ms);

/* One synchronisation exchange. Caller decides when to call it. */
ntplite_error_t ntplite_client_sync(const ntplite_client_t *cli, ntplite_result_t *out);

#endif /* ntplite_CLIENT_H */
