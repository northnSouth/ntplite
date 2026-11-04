#ifndef NTPLITE_COMMON_H
#define NTPLITE_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

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
#define NTPLITE_PACKET_SIZE   24u   /* t1, t2, t3 as 6 x uint32_t, LE */

/* Replies read per sync while discarding stale ones (late replies from
 * earlier timed-out exchanges). */
#ifndef NTPLITE_MAX_RECV_ATTEMPTS
#  define NTPLITE_MAX_RECV_ATTEMPTS  4u
#endif

/* All values are non-negative. */
typedef enum {
    NTPLITE_OK               = 0,
    NTPLITE_ERR_INVALID      = 1,  /* bad argument or timestamp          */
    NTPLITE_ERR_NEGATIVE_RTT = 2,  /* delta < 0, exchange rejected       */
    NTPLITE_ERR_BUFFER       = 3,  /* buffer shorter than a packet       */
    NTPLITE_ERR_MISMATCH     = 4,  /* no reply echoed our t1             */
    NTPLITE_ERR_TRANSPORT    = 5,  /* send/recv hook failed or timed out */
    NTPLITE_ERR_DELAY        = 6,  /* delta above max_delta, discarded   */
} ntplite_result_t;

typedef struct {
    int32_t  sec;
    uint32_t nsec;
} ntplite_time_t;

/* Same representation; the alias only documents intent. */
typedef ntplite_time_t ntplite_duration_t;

/* Hooks. Each receives the user_arg given at init. */
typedef ntplite_time_t   (*ntplite_time_fn)(void *arg);
typedef ntplite_result_t (*ntplite_send_fn)(const uint8_t *buf, size_t len,
                                            void *arg);
typedef ntplite_result_t (*ntplite_recv_fn)(uint8_t *buf, size_t len,
                                            void *arg);

typedef struct {
    uint32_t t1_sec, t1_nsec;
    uint32_t t2_sec, t2_nsec;
    uint32_t t3_sec, t3_nsec;
} ntplite_packet_t;

/* Arithmetic on normalized values (works for both times and durations). */
bool           ntplite_is_valid(ntplite_time_t t);          /* sec>=0, nsec<1e9 */
ntplite_time_t ntplite_add (ntplite_time_t a, ntplite_time_t b);
ntplite_time_t ntplite_sub (ntplite_time_t a, ntplite_time_t b);
ntplite_time_t ntplite_neg (ntplite_time_t d);
ntplite_time_t ntplite_abs (ntplite_time_t d);
ntplite_time_t ntplite_shr (ntplite_time_t d, uint8_t k);   /* d / 2^k */
int            ntplite_sign(ntplite_time_t d);              /* -1, 0, 1 */
int            ntplite_cmp (ntplite_time_t a, ntplite_time_t b);

ntplite_result_t ntplite_packet_encode(const ntplite_packet_t *pkt,
                                       uint8_t *buf, size_t len);
ntplite_result_t ntplite_packet_decode(const uint8_t *buf, size_t len,
                                       ntplite_packet_t *pkt);

/**
 * delta = (t4 - t1) - (t3 - t2)          -> NTPLITE_ERR_NEGATIVE_RTT if < 0
 * theta = ((t2 - t1) + (t3 - t4)) / 2    (server minus client)
 */
ntplite_result_t ntplite_offset(ntplite_time_t t1, ntplite_time_t t2,
                                ntplite_time_t t3, ntplite_time_t t4,
                                ntplite_duration_t *delta,
                                ntplite_duration_t *theta);

#ifdef __cplusplus
}
#endif
#endif /* NTPLITE_COMMON_H */