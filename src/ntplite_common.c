#include "ntplite_common.h"

bool ntplite_is_valid(ntplite_time_t t)
{
    return t.sec >= 0 && t.nsec < NTPLITE_NSEC_PER_SEC;
}

int ntplite_sign(ntplite_time_t d)
{
    if (d.sec < 0) return -1;
    if (d.sec > 0) return  1;
    return d.nsec ? 1 : 0;
}

ntplite_time_t ntplite_neg(ntplite_time_t d)
{
    ntplite_time_t r;
    if (d.nsec == 0) {
        r.sec  = -d.sec;
        r.nsec = 0;
    } else {
        r.sec  = -d.sec - 1;
        r.nsec = NTPLITE_NSEC_PER_SEC - d.nsec;
    }
    return r;
}

/* Normalized operands: nsec sum < 2e9, so a single carry suffices. */
ntplite_time_t ntplite_add(ntplite_time_t a, ntplite_time_t b)
{
    ntplite_time_t r;
    r.sec  = a.sec + b.sec;
    r.nsec = a.nsec + b.nsec;
    if (r.nsec >= NTPLITE_NSEC_PER_SEC) {
        r.nsec -= NTPLITE_NSEC_PER_SEC;
        r.sec  += 1;
    }
    return r;
}

ntplite_time_t ntplite_sub(ntplite_time_t a, ntplite_time_t b)
{
    return ntplite_add(a, ntplite_neg(b));
}

ntplite_time_t ntplite_abs(ntplite_time_t d)
{
    return (ntplite_sign(d) < 0) ? ntplite_neg(d) : d;
}

int ntplite_cmp(ntplite_time_t a, ntplite_time_t b)
{
    return ntplite_sign(ntplite_sub(a, b));
}

/* d / 2 in 32-bit math: an odd sec contributes 0.5 s to nsec. */
static ntplite_time_t half(ntplite_time_t d)
{
    uint32_t odd = (uint32_t)d.sec & 1u;
    ntplite_time_t r;
    r.sec  = (d.sec - (int32_t)odd) / 2;
    r.nsec = d.nsec / 2u + (odd ? NTPLITE_NSEC_PER_SEC / 2u : 0u);
    return r;
}

ntplite_time_t ntplite_shr(ntplite_time_t d, uint8_t k)
{
    while (k--) d = half(d);
    return d;
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

ntplite_result_t ntplite_packet_encode(const ntplite_packet_t *pkt,
                                       uint8_t *buf, size_t len)
{
    if (!pkt || !buf)                return NTPLITE_ERR_INVALID;
    if (len < NTPLITE_PACKET_SIZE)   return NTPLITE_ERR_BUFFER;

    put_u32(buf +  0, pkt->t1_sec);  put_u32(buf +  4, pkt->t1_nsec);
    put_u32(buf +  8, pkt->t2_sec);  put_u32(buf + 12, pkt->t2_nsec);
    put_u32(buf + 16, pkt->t3_sec);  put_u32(buf + 20, pkt->t3_nsec);
    return NTPLITE_OK;
}

ntplite_result_t ntplite_packet_decode(const uint8_t *buf, size_t len,
                                       ntplite_packet_t *pkt)
{
    if (!buf || !pkt)                return NTPLITE_ERR_INVALID;
    if (len < NTPLITE_PACKET_SIZE)   return NTPLITE_ERR_BUFFER;

    pkt->t1_sec = get_u32(buf +  0);  pkt->t1_nsec = get_u32(buf +  4);
    pkt->t2_sec = get_u32(buf +  8);  pkt->t2_nsec = get_u32(buf + 12);
    pkt->t3_sec = get_u32(buf + 16);  pkt->t3_nsec = get_u32(buf + 20);
    return NTPLITE_OK;
}

ntplite_result_t ntplite_offset(ntplite_time_t t1, ntplite_time_t t2,
                                ntplite_time_t t3, ntplite_time_t t4,
                                ntplite_duration_t *delta,
                                ntplite_duration_t *theta)
{
    if (!delta || !theta) return NTPLITE_ERR_INVALID;

    ntplite_duration_t d =
        ntplite_sub(ntplite_sub(t4, t1), ntplite_sub(t3, t2));
    if (ntplite_sign(d) < 0)
        return NTPLITE_ERR_NEGATIVE_RTT;

    *delta = d;
    *theta = half(ntplite_add(ntplite_sub(t2, t1), ntplite_sub(t3, t4)));
    return NTPLITE_OK;
}