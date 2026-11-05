#ifndef NTPLITE_COMMON_H
#define NTPLITE_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Time format follows ROS2 builtin_interfaces: {int32_t sec, uint32_t nsec},
 * nsec always in [0, 999 999 999], value = sec + nsec / 1e9.
 * Example: -1.5 s is {-2, 500000000}.
 *
 * Caution: with a Unix epoch, int32_t sec overflows in January 2038.
 */

#define NTPLITE_NSEC_PER_SEC  UINT32_C(1000000000)
#define NTPLITE_PACKET_SIZE   24u   /* t1, t2, t3 as 6 x uint32_t, little endian */

/* All values are non-negative. */
typedef enum {
    NTPLITE_OK               = 0,
    NTPLITE_ERR_INVALID      = 1,  /* bad argument, clock reading or reply timestamp,
                                      or an offset beyond +-2^30 s                */
    NTPLITE_ERR_NEGATIVE_RTT = 2,  /* delta < 0, exchange rejected                */
    NTPLITE_ERR_BUFFER       = 3,  /* buffer shorter than a packet                */
    NTPLITE_ERR_MISMATCH     = 4,  /* reply does not answer the request in flight */
    NTPLITE_ERR_TRANSPORT    = 5,  /* server send hook failed                     */
    NTPLITE_ERR_DELAY        = 6,  /* delta above max_delta, discarded            */
} ntplite_result_t;

typedef struct {
    int32_t  sec;
    uint32_t nsec;
} ntplite_time_t;

/* Same representation; the alias only documents intent. */
typedef ntplite_time_t ntplite_duration_t;

/*
 * Platform hooks. Each receives the user_arg given at init.
 *
 * time_fn  Current raw clock, ideally monotonic. Return an invalid value
 *          (sec < 0 or nsec >= 1e9) to signal a failed read.
 * send_fn  Server only: transmit exactly `len` bytes as one packet. Any result
 *          other than NTPLITE_OK is reported as NTPLITE_ERR_TRANSPORT.
 */
typedef ntplite_time_t   (*ntplite_time_fn)(void *arg);
typedef ntplite_result_t (*ntplite_send_fn)(const uint8_t *buf, size_t len, void *arg);

/* A read-only byte range. */
typedef struct {
    const uint8_t *data;
    size_t         len;
} ntplite_span_t;

/*
 * Arithmetic on normalized values (times and durations alike). On overflow,
 * seconds wrap modulo 2^32 instead of invoking undefined behaviour; results
 * are only meaningful while they fit int32_t.
 */
bool           ntplite_is_valid(ntplite_time_t t);          /* sec >= 0, nsec < 1e9 */
ntplite_time_t ntplite_add (ntplite_time_t a, ntplite_time_t b);
ntplite_time_t ntplite_sub (ntplite_time_t a, ntplite_time_t b);
ntplite_time_t ntplite_neg (ntplite_time_t d);
ntplite_time_t ntplite_abs (ntplite_time_t d);
ntplite_time_t ntplite_shr (ntplite_time_t d, uint8_t k);   /* floor(d / 2^k) */
int            ntplite_sign(ntplite_time_t d);              /* -1, 0, 1 */
int            ntplite_cmp (ntplite_time_t a, ntplite_time_t b);

#ifdef __cplusplus
}
#endif
#endif /* NTPLITE_COMMON_H */
