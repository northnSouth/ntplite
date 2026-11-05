#include "ntplite_test_util.hpp"

#include <cstdlib>
#include <string>

// =========================================================================
// ntplite_is_valid
// =========================================================================

TEST(IsValid, ZeroIsValid)       { EXPECT_TRUE (ntplite_is_valid(mk(0, 0))); }
TEST(IsValid, MaxLegalNsec)      { EXPECT_TRUE (ntplite_is_valid(mk(0, 999999999u))); }
TEST(IsValid, NsecAtBoundary)    { EXPECT_FALSE(ntplite_is_valid(mk(0, 1000000000u))); }
TEST(IsValid, NsecOverflow)      { EXPECT_FALSE(ntplite_is_valid(mk(0, 0xFFFFFFFFu))); }
TEST(IsValid, NegativeSecIsInvalid) {
    EXPECT_FALSE(ntplite_is_valid(mk(-1, 0)));
    EXPECT_FALSE(ntplite_is_valid(mk(-1, 500000000u)));
    EXPECT_FALSE(ntplite_is_valid(mk(INT32_MIN, 0)));
}
TEST(IsValid, LargePositiveSecIsValid) {
    EXPECT_TRUE(ntplite_is_valid(mk(INT32_MAX, 999999999u)));
}

// =========================================================================
// ntplite_sign
// =========================================================================

TEST(Sign, Zero)            { EXPECT_EQ(ntplite_sign(mk(0, 0)),  0); }
TEST(Sign, PositiveNsecOnly){ EXPECT_EQ(ntplite_sign(mk(0, 1)),  1); }
TEST(Sign, PositiveSec)     { EXPECT_EQ(ntplite_sign(mk(1, 0)),  1); }
TEST(Sign, PositiveBoth)    { EXPECT_EQ(ntplite_sign(mk(1, 500000000u)), 1); }
TEST(Sign, NegativeSecZeroNsec)   { EXPECT_EQ(ntplite_sign(mk(-1, 0)),          -1); }
TEST(Sign, NegativeSecNonzeroNsec){ EXPECT_EQ(ntplite_sign(mk(-1, 999999999u)), -1); }

// =========================================================================
// ntplite_neg  — floor convention: nsec always in [0, 1e9)
// =========================================================================

TEST(Neg, ZeroIsItsOwnNegative)
{
    EXPECT_TRUE(TimeIs(ntplite_neg(mk(0, 0)), 0, 0));
}

TEST(Neg, PureIntegerSeconds)
{
    EXPECT_TRUE(TimeIs(ntplite_neg(mk( 3, 0)), -3, 0));
    EXPECT_TRUE(TimeIs(ntplite_neg(mk(-3, 0)),  3, 0));
}

TEST(Neg, NonZeroNsecFollowsFloorConvention)
{
    // -1.5 s is {-2, 500000000}
    EXPECT_TRUE(TimeIs(ntplite_neg(mk( 1, 500000000u)), -2, 500000000u));
    EXPECT_TRUE(TimeIs(ntplite_neg(mk(-2, 500000000u)),  1, 500000000u));
}

TEST(Neg, SmallNsec)
{
    // neg({0, 1}) = {-1, 999999999}
    EXPECT_TRUE(TimeIs(ntplite_neg(mk( 0,         1u)), -1, 999999999u));
    EXPECT_TRUE(TimeIs(ntplite_neg(mk(-1, 999999999u)),  0,         1u));
}

TEST(Neg, DoubleNegIsIdentity)
{
    const ntplite_time_t cases[] = {
        mk(0, 0), mk(1, 0), mk(0, 1), mk(3, 500000000u),
        mk(-1, 0), mk(-2, 500000000u), mk(-1, 999999999u),
    };
    for (auto t : cases)
        EXPECT_TRUE(TimeIs(ntplite_neg(ntplite_neg(t)), t.sec, t.nsec))
            << "at {" << t.sec << ", " << t.nsec << "}";
}

// =========================================================================
// ntplite_add / ntplite_sub
// =========================================================================

TEST(Add, NsecCarryNormalization)
{
    // {0, 600ms} + {0, 600ms} = {1, 200ms}
    EXPECT_TRUE(TimeIs(ntplite_add(mk(0, 600000000u), mk(0, 600000000u)),
                       1, 200000000u));
}

TEST(Add, SecAndNsecCarry)
{
    // {2, 300ms} + {1, 800ms} = {4, 100ms}
    EXPECT_TRUE(TimeIs(ntplite_add(mk(2, 300000000u), mk(1, 800000000u)),
                       4, 100000000u));
}

TEST(Add, Commutativity)
{
    ntplite_time_t a = mk(3, 100000000u), b = mk(-1, 700000000u);
    auto ab = ntplite_add(a, b), ba = ntplite_add(b, a);
    EXPECT_EQ(ab.sec, ba.sec);
    EXPECT_EQ(ab.nsec, ba.nsec);
}

TEST(Sub, SubSelfIsZero)
{
    for (auto t : { mk(0, 0), mk(5, 123456789u), mk(-3, 500000000u) })
        EXPECT_EQ(ntplite_sign(ntplite_sub(t, t)), 0)
            << "{" << t.sec << ", " << t.nsec << "}";
}

TEST(Sub, Antisymmetry)
{
    // sub(a,b) == neg(sub(b,a))
    ntplite_time_t a = mk(5, 300000000u), b = mk(3, 700000000u);
    auto lhs = ntplite_sub(a, b);
    auto rhs = ntplite_neg(ntplite_sub(b, a));
    EXPECT_EQ(lhs.sec, rhs.sec);
    EXPECT_EQ(lhs.nsec, rhs.nsec);
}

TEST(Sub, BorrowAcrossSecond)
{
    // {3, 0} - {0, 1} = {2, 999999999}
    EXPECT_TRUE(TimeIs(ntplite_sub(mk(3, 0), mk(0, 1)), 2, 999999999u));
}

// =========================================================================
// ntplite_abs
// =========================================================================

TEST(Abs, PositiveUnchanged)   { EXPECT_TRUE(TimeIs(ntplite_abs(mk(2, 300000000u)), 2, 300000000u)); }
TEST(Abs, ZeroUnchanged)       { EXPECT_TRUE(TimeIs(ntplite_abs(mk(0, 0)), 0, 0)); }
TEST(Abs, NegativeBecomesPositive)
{
    // abs({-2, 500000000}) = abs(-1.5 s) = {1, 500000000}
    EXPECT_TRUE(TimeIs(ntplite_abs(mk(-2, 500000000u)), 1, 500000000u));
    EXPECT_TRUE(TimeIs(ntplite_abs(mk(-1, 0)),          1, 0));
}
TEST(Abs, ResultAlwaysNonNegative)
{
    for (auto t : { mk(-3, 0), mk(-1, 999999999u), mk(-2, 500000000u) })
        EXPECT_GE(ntplite_sign(ntplite_abs(t)), 0);
}

// =========================================================================
// ntplite_cmp
// =========================================================================

TEST(Cmp, Reflexive)
{
    EXPECT_EQ(ntplite_cmp(mk(5, 0),      mk(5, 0)),      0);
    EXPECT_EQ(ntplite_cmp(mk(0, 1),      mk(0, 1)),      0);
    EXPECT_EQ(ntplite_cmp(mk(-1, 500000000u), mk(-1, 500000000u)), 0);
}

TEST(Cmp, SecDominatesNsec)
{
    // {2, 0} > {1, 999999999}
    EXPECT_EQ(ntplite_cmp(mk(2, 0), mk(1, 999999999u)),  1);
    EXPECT_EQ(ntplite_cmp(mk(1, 999999999u), mk(2, 0)), -1);
}

TEST(Cmp, NsecTiebreaker)
{
    EXPECT_EQ(ntplite_cmp(mk(1, 2), mk(1, 1)),  1);
    EXPECT_EQ(ntplite_cmp(mk(1, 1), mk(1, 2)), -1);
}

TEST(Cmp, Antisymmetric)
{
    ntplite_time_t a = mk(3, 0), b = mk(2, 999999999u);
    EXPECT_EQ(ntplite_cmp(a, b),  1);
    EXPECT_EQ(ntplite_cmp(b, a), -1);
}

// =========================================================================
// ntplite_shr
// =========================================================================

TEST(Shr, K0IsIdentity)
{
    ntplite_time_t t = mk(7, 300000000u);
    EXPECT_TRUE(TimeIs(ntplite_shr(t, 0), t.sec, t.nsec));
}

TEST(Shr, K1HalvesEvenSec)         { EXPECT_TRUE(TimeIs(ntplite_shr(mk(4, 0), 1), 2, 0)); }
TEST(Shr, K1OddSecCarriesToNsec)   { EXPECT_TRUE(TimeIs(ntplite_shr(mk(3, 0), 1), 1, 500000000u)); }
TEST(Shr, K1PureNsec)              { EXPECT_TRUE(TimeIs(ntplite_shr(mk(1, 0), 1), 0, 500000000u)); }

TEST(Shr, NegativeValue)
{
    // {-2, 0} / 2 = {-1, 0}
    EXPECT_TRUE(TimeIs(ntplite_shr(mk(-2, 0), 1), -1, 0));
    // {-1, 0} / 2 = {-1, 500000000}  (== -0.5 s, floor of -0.5)
    EXPECT_TRUE(TimeIs(ntplite_shr(mk(-1, 0), 1), -1, 500000000u));
}

TEST(Shr, LargeKConvergesToZero)
{
    // 1 ns >> 30 must be 0
    EXPECT_EQ(ntplite_sign(ntplite_shr(mk(0, 1), 30)), 0);
}

TEST(Shr, HalvingAndDoublingApprox)
{
    // shr(d, 1) + shr(d, 1) should be within 1 ns of d (integer rounding)
    ntplite_time_t d = mk(7, 1);
    ntplite_time_t half = ntplite_shr(d, 1);
    ntplite_time_t reconstructed = ntplite_add(half, half);
    int64_t err = std::abs(to_ns(d) - to_ns(reconstructed));
    EXPECT_LE(err, 1);
}

// =========================================================================
// ntplite_offset  — verify the NTP formula with concrete values
// =========================================================================

TEST(Offset, NullPointers)
{
    ntplite_duration_t delta, theta;
    ntplite_time_t z = mk(0, 0);
    EXPECT_EQ(ntplite_offset(z, z, z, z, nullptr, &theta), NTPLITE_ERR_INVALID);
    EXPECT_EQ(ntplite_offset(z, z, z, z, &delta, nullptr), NTPLITE_ERR_INVALID);
}

TEST(Offset, SymmetricPathZeroOffset)
{
    // t1=0, t2=0.5, t3=0.5, t4=1 → delta=1, theta=0
    ntplite_duration_t delta, theta;
    ASSERT_EQ(ntplite_offset(to_ts(0), to_ts(500*kMs), to_ts(500*kMs),
                             to_ts(kNs), &delta, &theta), NTPLITE_OK);
    EXPECT_EQ(to_ns(delta), kNs);
    EXPECT_EQ(ntplite_sign(theta), 0);
}

TEST(Offset, SymmetricPathPositiveOffset)
{
    // server 5 s ahead, symmetric 1 s RTT
    // t1=0, t2=5.5, t3=5.5, t4=1 → delta=1, theta=5
    ntplite_duration_t delta, theta;
    ASSERT_EQ(ntplite_offset(to_ts(0), to_ts(5*kNs + 500*kMs),
                             to_ts(5*kNs + 500*kMs), to_ts(kNs),
                             &delta, &theta), NTPLITE_OK);
    EXPECT_EQ(to_ns(delta), kNs);
    EXPECT_EQ(to_ns(theta), 5 * kNs);
}

TEST(Offset, AsymmetricPathOffsetBiasedByHalfAsymmetry)
{
    // fwd=3ms, back=1ms, server offset=0
    // theta must equal (fwd - back) / 2 = 1ms
    ntplite_duration_t delta, theta;
    int64_t fwd = 3*kMs, back = 1*kMs;
    ASSERT_EQ(ntplite_offset(to_ts(0), to_ts(fwd), to_ts(fwd),
                             to_ts(fwd + back), &delta, &theta), NTPLITE_OK);
    EXPECT_EQ(to_ns(delta), fwd + back);
    EXPECT_EQ(to_ns(theta), (fwd - back) / 2);
}

TEST(Offset, ZeroRtt)
{
    // t1=t4, t2=t3 (instantaneous round-trip): delta=0, theta=server offset
    ntplite_duration_t delta, theta;
    ASSERT_EQ(ntplite_offset(to_ts(5*kNs), to_ts(10*kNs), to_ts(10*kNs),
                             to_ts(5*kNs), &delta, &theta), NTPLITE_OK);
    EXPECT_EQ(ntplite_sign(delta), 0);
    EXPECT_EQ(to_ns(theta), 5 * kNs);
}

TEST(Offset, NegativeDeltaRejected)
{
    // Server claims more processing time than the RTT: t3-t2 > t4-t1
    ntplite_duration_t delta, theta;
    EXPECT_EQ(ntplite_offset(to_ts(0), to_ts(kNs), to_ts(10*kNs),
                             to_ts(5*kNs), &delta, &theta),
              NTPLITE_ERR_NEGATIVE_RTT);
}

// =========================================================================
// ntplite_packet_encode / decode
// =========================================================================

TEST(Packet, NullArguments)
{
    uint8_t buf[NTPLITE_PACKET_SIZE] = {};
    ntplite_packet_t p{};
    EXPECT_EQ(ntplite_packet_encode(nullptr, buf, sizeof(buf)), NTPLITE_ERR_INVALID);
    EXPECT_EQ(ntplite_packet_encode(&p, nullptr, sizeof(buf)), NTPLITE_ERR_INVALID);
    EXPECT_EQ(ntplite_packet_decode(nullptr, sizeof(buf), &p), NTPLITE_ERR_INVALID);
    EXPECT_EQ(ntplite_packet_decode(buf, sizeof(buf), nullptr), NTPLITE_ERR_INVALID);
}

TEST(Packet, ShortBufferRejected)
{
    uint8_t buf[NTPLITE_PACKET_SIZE] = {};
    ntplite_packet_t p{};
    EXPECT_EQ(ntplite_packet_encode(&p, buf, NTPLITE_PACKET_SIZE - 1), NTPLITE_ERR_BUFFER);
    EXPECT_EQ(ntplite_packet_decode(buf, NTPLITE_PACKET_SIZE - 1, &p), NTPLITE_ERR_BUFFER);
}

TEST(Packet, AllFieldsRoundTrip)
{
    ntplite_packet_t orig{};
    orig.t1_sec  = 0xDEADBEEFu; orig.t1_nsec = 0x12345678u;
    orig.t2_sec  = 0x0A0B0C0Du; orig.t2_nsec = 999999999u;
    orig.t3_sec  = 0xFFFFFFFFu; orig.t3_nsec = 0u;

    uint8_t buf[NTPLITE_PACKET_SIZE];
    ASSERT_EQ(ntplite_packet_encode(&orig, buf, sizeof(buf)), NTPLITE_OK);

    ntplite_packet_t got{};
    ASSERT_EQ(ntplite_packet_decode(buf, sizeof(buf), &got), NTPLITE_OK);

    EXPECT_EQ(got.t1_sec,  orig.t1_sec);
    EXPECT_EQ(got.t1_nsec, orig.t1_nsec);
    EXPECT_EQ(got.t2_sec,  orig.t2_sec);
    EXPECT_EQ(got.t2_nsec, orig.t2_nsec);
    EXPECT_EQ(got.t3_sec,  orig.t3_sec);
    EXPECT_EQ(got.t3_nsec, orig.t3_nsec);
}

TEST(Packet, WireFormatIsLittleEndian)
{
    ntplite_packet_t p{};
    p.t1_sec = 0x01020304u;   // LE on wire: 04 03 02 01
    p.t1_nsec = 0x05060708u;  // LE on wire: 08 07 06 05

    uint8_t buf[NTPLITE_PACKET_SIZE] = {};
    ASSERT_EQ(ntplite_packet_encode(&p, buf, sizeof(buf)), NTPLITE_OK);

    EXPECT_EQ(buf[0], 0x04u); EXPECT_EQ(buf[1], 0x03u);
    EXPECT_EQ(buf[2], 0x02u); EXPECT_EQ(buf[3], 0x01u);
    EXPECT_EQ(buf[4], 0x08u); EXPECT_EQ(buf[5], 0x07u);
    EXPECT_EQ(buf[6], 0x06u); EXPECT_EQ(buf[7], 0x05u);
}

TEST(Packet, FieldsOccupyDistinctOffsets)
{
    // Give each field a unique bit pattern; verify no aliasing after decode.
    ntplite_packet_t p{};
    p.t1_sec  = 0x11111111u; p.t1_nsec = 0x22222222u;
    p.t2_sec  = 0x33333333u; p.t2_nsec = 0x44444444u;
    p.t3_sec  = 0x55555555u; p.t3_nsec = 0x66666666u;

    uint8_t buf[NTPLITE_PACKET_SIZE];
    ASSERT_EQ(ntplite_packet_encode(&p, buf, sizeof(buf)), NTPLITE_OK);

    ntplite_packet_t q{};
    ASSERT_EQ(ntplite_packet_decode(buf, sizeof(buf), &q), NTPLITE_OK);

    EXPECT_EQ(q.t1_sec,  p.t1_sec);  EXPECT_EQ(q.t1_nsec, p.t1_nsec);
    EXPECT_EQ(q.t2_sec,  p.t2_sec);  EXPECT_EQ(q.t2_nsec, p.t2_nsec);
    EXPECT_EQ(q.t3_sec,  p.t3_sec);  EXPECT_EQ(q.t3_nsec, p.t3_nsec);
}

TEST(Packet, ChangingOneFieldDoesNotCorruptOthers)
{
    ntplite_packet_t base_pkt{};
    base_pkt.t1_sec = 1; base_pkt.t1_nsec = 10;
    base_pkt.t2_sec = 2; base_pkt.t2_nsec = 20;
    base_pkt.t3_sec = 3; base_pkt.t3_nsec = 30;

    uint8_t buf[NTPLITE_PACKET_SIZE];
    ASSERT_EQ(ntplite_packet_encode(&base_pkt, buf, sizeof(buf)), NTPLITE_OK);

    // Flip just t2_sec in the buffer (at offset 8), re-decode
    buf[8] ^= 0xFF;
    ntplite_packet_t got{};
    ASSERT_EQ(ntplite_packet_decode(buf, sizeof(buf), &got), NTPLITE_OK);

    EXPECT_EQ(got.t1_sec,  base_pkt.t1_sec);   // t1 untouched
    EXPECT_EQ(got.t1_nsec, base_pkt.t1_nsec);
    EXPECT_NE(got.t2_sec,  base_pkt.t2_sec);    // t2_sec changed
    EXPECT_EQ(got.t2_nsec, base_pkt.t2_nsec);   // t2_nsec untouched
    EXPECT_EQ(got.t3_sec,  base_pkt.t3_sec);    // t3 untouched
    EXPECT_EQ(got.t3_nsec, base_pkt.t3_nsec);
}

// =========================================================================
// Arithmetic against an independent int64 reference
//
// Every operand (pair) from a grid of edge values: second boundaries, sign
// changes, odd/even seconds, +-2^29 s. All results fit int32_t seconds, so
// the library must agree with plain int64 nanosecond math exactly and return
// normalized values.
// =========================================================================

namespace {

std::vector<ntplite_time_t> RefGrid()
{
    static const int32_t  secs[]  = { -(1 << 29), -1001, -2, -1, 0, 1, 2, 1001, (1 << 29) - 1 };
    static const uint32_t nsecs[] = { 0u, 1u, 499999999u, 500000000u, 999999999u };
    std::vector<ntplite_time_t> v;
    for (int32_t s : secs)
        for (uint32_t n : nsecs)
            v.push_back(mk(s, n));
    return v;
}

int RefSign(int64_t v) { return (v > 0) - (v < 0); }

std::string Str(ntplite_time_t t)
{
    return "{" + std::to_string(t.sec) + ", " + std::to_string(t.nsec) + "}";
}

void ExpectNs(ntplite_time_t got, int64_t want)
{
    EXPECT_LT(got.nsec, NTPLITE_NSEC_PER_SEC) << "not normalized: " << Str(got);
    EXPECT_EQ(to_ns(got), want);
}

} // namespace

TEST(ArithRef, UnaryOpsMatchReference)
{
    for (ntplite_time_t a : RefGrid()) {
        SCOPED_TRACE(Str(a));
        const int64_t x = to_ns(a);

        EXPECT_EQ(ntplite_sign(a), RefSign(x));
        ExpectNs(ntplite_neg(a), -x);
        ExpectNs(ntplite_abs(a), x < 0 ? -x : x);
        for (uint8_t k = 0; k <= 31; k++) {
            SCOPED_TRACE("k=" + std::to_string(k));
            ExpectNs(ntplite_shr(a, k), floor_div(x, int64_t(1) << k));
        }
    }
}

TEST(ArithRef, BinaryOpsMatchReference)
{
    const std::vector<ntplite_time_t> grid = RefGrid();
    for (ntplite_time_t a : grid) {
        for (ntplite_time_t b : grid) {
            SCOPED_TRACE(Str(a) + " , " + Str(b));
            const int64_t x = to_ns(a), y = to_ns(b);

            ExpectNs(ntplite_add(a, b), x + y);
            ExpectNs(ntplite_sub(a, b), x - y);
            EXPECT_EQ(ntplite_cmp(a, b), RefSign(x - y));
        }
    }
}

// =========================================================================
// Extremes: defined wrap-around instead of undefined behaviour.
// Under UBSan with -fno-sanitize-recover, signed overflow would abort here.
// =========================================================================

TEST(ArithExtreme, AddWrapsAtInt32Max)
{
    EXPECT_TRUE(TimeIs(ntplite_add(mk(INT32_MAX, 999999999u), mk(0, 1u)), INT32_MIN, 0));
    EXPECT_TRUE(TimeIs(ntplite_add(mk(INT32_MAX, 0), mk(INT32_MAX, 0)), -2, 0));
}

TEST(ArithExtreme, NegOfInt32MinWraps)
{
    EXPECT_TRUE(TimeIs(ntplite_neg(mk(INT32_MIN, 0)), INT32_MIN, 0));
}

TEST(ArithExtreme, SubAcrossTheWholeRangeIsDefined)
{
    (void)ntplite_sub(mk(INT32_MAX, 999999999u), mk(INT32_MIN, 0));
    (void)ntplite_sub(mk(INT32_MIN, 0), mk(INT32_MAX, 999999999u));
}

TEST(ArithExtreme, CmpIsExactAcrossTheWholeRange)
{
    EXPECT_EQ(ntplite_cmp(mk(INT32_MAX, 0), mk(INT32_MIN, 0)),  1);
    EXPECT_EQ(ntplite_cmp(mk(INT32_MIN, 0), mk(INT32_MAX, 0)), -1);
    EXPECT_EQ(ntplite_cmp(mk(INT32_MAX, 999999999u), mk(-1, 0)), 1);
}

// =========================================================================
// ntplite_offset: per-leg offset limit [-2^30, 2^30) s
// =========================================================================

TEST(Offset, ServerAheadAtLimitIsRejected)
{
    const ntplite_time_t c = mk(0, 0), s = mk(NTPLITE_MAX_OFFSET_SEC, 0);
    ntplite_duration_t delta, theta;
    EXPECT_EQ(ntplite_offset(c, s, s, c, &delta, &theta), NTPLITE_ERR_INVALID);
}

TEST(Offset, ServerAheadJustBelowLimitIsAccepted)
{
    const ntplite_time_t c = mk(0, 0), s = mk(NTPLITE_MAX_OFFSET_SEC - 1, 999999999u);
    ntplite_duration_t delta, theta;
    ASSERT_EQ(ntplite_offset(c, s, s, c, &delta, &theta), NTPLITE_OK);
    EXPECT_EQ(ntplite_sign(delta), 0);
    EXPECT_TRUE(TimeIs(theta, NTPLITE_MAX_OFFSET_SEC - 1, 999999999u));
}

TEST(Offset, ServerBehindAtLimitIsAccepted)
{
    const ntplite_time_t c = mk(NTPLITE_MAX_OFFSET_SEC, 0), s = mk(0, 0);
    ntplite_duration_t delta, theta;
    ASSERT_EQ(ntplite_offset(c, s, s, c, &delta, &theta), NTPLITE_OK);
    EXPECT_TRUE(TimeIs(theta, -NTPLITE_MAX_OFFSET_SEC, 0));
}

TEST(Offset, ServerBehindBeyondLimitIsRejected)
{
    const ntplite_time_t c = mk(NTPLITE_MAX_OFFSET_SEC, 1u), s = mk(0, 0);
    ntplite_duration_t delta, theta;
    EXPECT_EQ(ntplite_offset(c, s, s, c, &delta, &theta), NTPLITE_ERR_INVALID);
}