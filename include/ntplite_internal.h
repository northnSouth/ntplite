#ifndef NTPLITE_INTERNAL_H
#define NTPLITE_INTERNAL_H

/* Wire format and offset math. Not installed and not part of the public API;
 * the tests reach it through the src/ include path. */

#include "ntplite_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Each leg's offset (t2 - t1, t3 - t4) must lie in [-2^30, 2^30) seconds,
 * about +-34 years. That keeps delta and theta inside int32_t seconds for any
 * reply a server can send. */
#define NTPLITE_MAX_OFFSET_SEC  (INT32_C(1) << 30)

typedef struct {
    uint32_t t1_sec, t1_nsec;
    uint32_t t2_sec, t2_nsec;
    uint32_t t3_sec, t3_nsec;
} ntplite_packet_t;

ntplite_result_t ntplite_packet_encode(const ntplite_packet_t *pkt,
                                       uint8_t *buf, size_t len);
ntplite_result_t ntplite_packet_decode(const uint8_t *buf, size_t len,
                                       ntplite_packet_t *pkt);

/**
 * delta = (t4 - t1) - (t3 - t2)          -> NTPLITE_ERR_NEGATIVE_RTT if < 0
 * theta = ((t2 - t1) + (t3 - t4)) / 2    (server minus client)
 * NTPLITE_ERR_INVALID if either leg's offset is outside NTPLITE_MAX_OFFSET_SEC.
 */
ntplite_result_t ntplite_offset(ntplite_time_t t1, ntplite_time_t t2,
                                ntplite_time_t t3, ntplite_time_t t4,
                                ntplite_duration_t *delta,
                                ntplite_duration_t *theta);

#ifdef __cplusplus
}
#endif
#endif /* NTPLITE_INTERNAL_H */