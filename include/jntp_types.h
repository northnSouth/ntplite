#ifndef JNTP_TYPES_H
#define JNTP_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* Fixed wire size of every request/response packet. */
#define JNTP_PACKET_SIZE 28u

/* JNTP_OK is 0; every error is a positive integer. */
typedef enum {
    JNTP_OK                   = 0,
    JNTP_ERR_INVALID_ARG      = 1,
    JNTP_ERR_CLOCK            = 2,
    JNTP_ERR_TRANSPORT        = 3,
    JNTP_ERR_BAD_LENGTH       = 4,
    JNTP_ERR_BAD_VERSION      = 5,
    JNTP_ERR_BAD_MODE         = 6,
    JNTP_ERR_BAD_TIMESTAMP    = 7,
    JNTP_ERR_ORIGIN_MISMATCH  = 8,
    JNTP_ERR_BAD_TIMING       = 9,
    JNTP_ERR_BUFFER_TOO_SMALL = 10
} jntp_error_t;

/* nsec must be < 1000000000. */
typedef struct {
    uint32_t sec;
    uint32_t nsec;
} jntp_timestamp_t;

/* Returns 0 on success, nonzero on failure. */
typedef struct {
    int (*now)(void *ctx, jntp_timestamp_t *out);
    void *ctx;
} jntp_clock_t;

#endif /* JNTP_TYPES_H */
