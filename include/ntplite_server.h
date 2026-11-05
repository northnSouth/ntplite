#ifndef NTPLITE_SERVER_H
#define NTPLITE_SERVER_H

#include "ntplite_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ntplite_time_fn time_fn;
    ntplite_send_fn send_fn;
    void           *user_arg;
} ntplite_server_platform_t;

/*
 * Stateless after init: ntplite_server_on_request() keeps nothing between
 * calls, so it may run concurrently on one context if time_fn and send_fn
 * allow it. For the same reason there are no debug counters (they would need
 * atomics); use the return value instead.
 */
typedef struct {
    ntplite_server_platform_t platform;
} ntplite_server_ctx_t;

ntplite_result_t ntplite_server_init(ntplite_server_ctx_t     *ctx,
                                     ntplite_server_platform_t platform);

/**
 * Call from your receive path with the raw request bytes (at least
 * NTPLITE_PACKET_SIZE; extra bytes are ignored). Stamps t2 first and t3 last,
 * then sends the reply through send_fn.
 */
ntplite_result_t ntplite_server_on_request(ntplite_server_ctx_t *ctx,
                                           const uint8_t *req, size_t req_len);

#ifdef __cplusplus
}
#endif
#endif /* NTPLITE_SERVER_H */