#ifndef ntplite_TYPES_H
#define ntplite_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* Fixed wire size of every request/response packet. */
#define ntplite_PACKET_SIZE 28u

/* ntplite_OK is 0; every error is a positive integer. */
typedef enum {
    ntplite_OK                   = 0,
    ntplite_ERR_INVALID_ARG      = 1,
    ntplite_ERR_CLOCK            = 2,
    ntplite_ERR_TRANSPORT        = 3,
    ntplite_ERR_BAD_LENGTH       = 4,
    ntplite_ERR_BAD_VERSION      = 5,
    ntplite_ERR_BAD_MODE         = 6,
    ntplite_ERR_BAD_TIMESTAMP    = 7,
    ntplite_ERR_ORIGIN_MISMATCH  = 8,
    ntplite_ERR_BAD_TIMING       = 9,
    ntplite_ERR_BUFFER_TOO_SMALL = 10
} ntplite_error_t;

/* nsec must be < 1000000000. */
typedef struct {
    uint32_t sec;
    uint32_t nsec;
} ntplite_timestamp_t;

/* Returns 0 on success, nonzero on failure. */
typedef struct {
    int (*now)(void *ctx, ntplite_timestamp_t *out);
    void *ctx;
} ntplite_clock_t;

#endif /* ntplite_TYPES_H */
