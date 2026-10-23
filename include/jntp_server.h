#ifndef JNTP_SERVER_H
#define JNTP_SERVER_H

#include "jntp_types.h"

typedef struct {
    jntp_clock_t clock;
} jntp_server_t;

jntp_error_t jntp_server_init(jntp_server_t *srv, const jntp_clock_t *clock);

/*
 * Transport-agnostic: feed in one received request, get one response to send.
 * On error *resp_len is 0 and nothing should be sent.
 */
jntp_error_t jntp_server_handle(const jntp_server_t *srv,
                              const uint8_t *req, size_t req_len,
                              uint8_t *resp, size_t resp_cap,
                              size_t *resp_len);

#endif /* JNTP_SERVER_H */
