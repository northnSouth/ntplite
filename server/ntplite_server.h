#ifndef ntplite_SERVER_H
#define ntplite_SERVER_H

#include "ntplite_types.h"

typedef struct {
    ntplite_clock_t clock;
} ntplite_server_t;

ntplite_error_t ntplite_server_init(ntplite_server_t *srv, const ntplite_clock_t *clock);

/*
 * Transport-agnostic: feed in one received request, get one response to send.
 * On error *resp_len is 0 and nothing should be sent.
 */
ntplite_error_t ntplite_server_handle(const ntplite_server_t *srv,
                              const uint8_t *req, size_t req_len,
                              uint8_t *resp, size_t resp_cap,
                              size_t *resp_len);

#endif /* ntplite_SERVER_H */
