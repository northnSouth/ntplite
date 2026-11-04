#include "ntplite_test_util.hpp"

namespace {
ntplite_time_t dummy_time(void *) { return mk(1, 0); }
ntplite_result_t dummy_send(const uint8_t *, size_t, void *) { return NTPLITE_OK; }
}

TEST(ServerInit, RejectsMissingContextOrHooks)
{
    ntplite_server_ctx_t c{};
    ntplite_server_platform_t good{};
    good.time_fn = dummy_time;
    good.send_fn = dummy_send;

    EXPECT_EQ(ntplite_server_init(&c, good), NTPLITE_OK);
    EXPECT_EQ(ntplite_server_init(nullptr, good), NTPLITE_ERR_INVALID);

    ntplite_server_platform_t p = good;
    p.time_fn = nullptr;
    EXPECT_EQ(ntplite_server_init(&c, p), NTPLITE_ERR_INVALID);

    p = good;
    p.send_fn = nullptr;
    EXPECT_EQ(ntplite_server_init(&c, p), NTPLITE_ERR_INVALID);
}

using ServerTest = NtpLiteSim;

TEST_F(ServerTest, EchoesT1AndStampsT2T3FromItsClock)
{
    offset = 7 * kNs + 500;   /* server clock = 1007 s + 500 ns */

    ntplite_packet_t req{};
    req.t1_sec  = 5;
    req.t1_nsec = 123;
    const Packet bytes = enc(req);

    ASSERT_EQ(ntplite_server_on_request(&srv, bytes.data(), bytes.size()), NTPLITE_OK);
    ASSERT_EQ(inbox.size(), 1u);

    const ntplite_packet_t rep = dec(inbox.front());
    EXPECT_EQ(rep.t1_sec, 5u);
    EXPECT_EQ(rep.t1_nsec, 123u);
    EXPECT_EQ(rep.t2_sec, 1007u);
    EXPECT_EQ(rep.t2_nsec, 500u);
    EXPECT_EQ(rep.t3_sec, 1007u);   /* no processing time in the sim */
    EXPECT_EQ(rep.t3_nsec, 500u);
}

TEST_F(ServerTest, T3ReflectsProcessingTime)
{
    srv_skew = 250;   /* the second clock read (t3) is 250 ns later */

    ntplite_packet_t req{};
    req.t1_sec = 1;
    const Packet bytes = enc(req);

    srv_reads = 0;
    ASSERT_EQ(ntplite_server_on_request(&srv, bytes.data(), bytes.size()), NTPLITE_OK);

    const ntplite_packet_t rep = dec(inbox.front());
    EXPECT_EQ(rep.t3_nsec - rep.t2_nsec, 250u);
}

TEST_F(ServerTest, RejectsShortRequestWithoutReplying)
{
    const Packet bytes{};
    EXPECT_EQ(ntplite_server_on_request(&srv, bytes.data(), bytes.size() - 1),
              NTPLITE_ERR_BUFFER);
    EXPECT_TRUE(inbox.empty());
}

TEST_F(ServerTest, RejectsNullArguments)
{
    const Packet bytes{};
    EXPECT_EQ(ntplite_server_on_request(nullptr, bytes.data(), bytes.size()),
              NTPLITE_ERR_INVALID);
    EXPECT_EQ(ntplite_server_on_request(&srv, nullptr, bytes.size()),
              NTPLITE_ERR_INVALID);
}

TEST_F(ServerTest, InvalidServerClockProducesNoReply)
{
    srv_bad_clock = true;
    const Packet bytes{};
    EXPECT_EQ(ntplite_server_on_request(&srv, bytes.data(), bytes.size()),
              NTPLITE_ERR_INVALID);
    EXPECT_TRUE(inbox.empty());
}

TEST_F(ServerTest, SendFailureIsPropagated)
{
    srv_send_fail = true;
    const Packet bytes{};
    EXPECT_EQ(ntplite_server_on_request(&srv, bytes.data(), bytes.size()),
              NTPLITE_ERR_TRANSPORT);
}

// -------------------------------------------------------------------------
// Timestamp correctness: order and field identity
// -------------------------------------------------------------------------

/* The t2_sec/t3_sec swap bug is invisible whenever t2.sec == t3.sec.
 * This test forces a second boundary between the two reads so the
 * sabotaged assignment pkt.t2_sec = (uint32_t)t3.sec produces a wrong value. */
TEST_F(ServerTest, T2SecIsNotConfusedWithT3SecAcrossSecondBoundary)
{
    // 1.5 s processing skew: t2 and t3 span a second boundary
    srv_skew  = static_cast<int64_t>(1500 * kMs);
    srv_reads = 0;

    ntplite_packet_t req{};
    req.t1_sec = 1;
    const Packet bytes = enc(req);
    ASSERT_EQ(ntplite_server_on_request(&srv, bytes.data(), bytes.size()), NTPLITE_OK);

    const ntplite_packet_t rep = dec(inbox.front());
    EXPECT_LT(rep.t2_sec, rep.t3_sec)
        << "t2_sec was clobbered with t3.sec";
}

/* Analogous check for the nsec fields within the same second. */
TEST_F(ServerTest, T2NsecIsFromEarlyStampNotLate)
{
    srv_skew  = 250 * kMs;   // sub-second: same .sec, different .nsec
    srv_reads = 0;

    ntplite_packet_t req{};
    req.t1_sec = 1;
    const Packet bytes = enc(req);
    ASSERT_EQ(ntplite_server_on_request(&srv, bytes.data(), bytes.size()), NTPLITE_OK);

    const ntplite_packet_t rep = dec(inbox.front());
    // t2 (early stamp) must have a smaller nsec than t3 (late stamp)
    EXPECT_EQ(rep.t2_sec, rep.t3_sec);
    EXPECT_LT(rep.t2_nsec, rep.t3_nsec)
        << "t2_nsec not from the early timestamp";
}

/* t2 <= t3 for every skew value. */
TEST_F(ServerTest, T2IsAlwaysLeqT3)
{
    for (int64_t skew_ms : { 0LL, 1LL, 100LL, 500LL, 1500LL }) {
        inbox.clear();
        srv_skew  = skew_ms * kMs;
        srv_reads = 0;

        ntplite_packet_t req{};
        req.t1_sec = 10;
        const Packet bytes = enc(req);
        ASSERT_EQ(ntplite_server_on_request(&srv, bytes.data(), bytes.size()), NTPLITE_OK);

        const ntplite_packet_t rep = dec(inbox.front());
        const int64_t t2_ns = (int64_t)rep.t2_sec * kNs + rep.t2_nsec;
        const int64_t t3_ns = (int64_t)rep.t3_sec * kNs + rep.t3_nsec;
        EXPECT_LE(t2_ns, t3_ns) << "skew_ms=" << skew_ms;
    }
}

/* Every bit of t1 must survive the round trip. */
TEST_F(ServerTest, T1EchoedExactlyAllBits)
{
    ntplite_packet_t req{};
    req.t1_sec  = 0xABCD1234u;
    req.t1_nsec = 0x0A0B0C0Du;

    const Packet bytes = enc(req);
    ASSERT_EQ(ntplite_server_on_request(&srv, bytes.data(), bytes.size()), NTPLITE_OK);

    const ntplite_packet_t rep = dec(inbox.front());
    EXPECT_EQ(rep.t1_sec,  req.t1_sec);
    EXPECT_EQ(rep.t1_nsec, req.t1_nsec);
}

/* Two back-to-back requests must produce independent, correctly matched replies. */
TEST_F(ServerTest, TwoConsecutiveRequestsDoNotCrossContaminate)
{
    ntplite_packet_t req1{}, req2{};
    req1.t1_sec = 10; req1.t1_nsec = 100;
    req2.t1_sec = 20; req2.t1_nsec = 200;

    ASSERT_EQ(ntplite_server_on_request(&srv, enc(req1).data(), NTPLITE_PACKET_SIZE), NTPLITE_OK);
    ASSERT_EQ(ntplite_server_on_request(&srv, enc(req2).data(), NTPLITE_PACKET_SIZE), NTPLITE_OK);
    ASSERT_EQ(inbox.size(), 2u);

    const ntplite_packet_t r1 = dec(inbox[0]);
    const ntplite_packet_t r2 = dec(inbox[1]);

    EXPECT_EQ(r1.t1_sec, 10u); EXPECT_EQ(r1.t1_nsec, 100u);
    EXPECT_EQ(r2.t1_sec, 20u); EXPECT_EQ(r2.t1_nsec, 200u);

    // Timestamps from r2 must be >= those from r1 (time moves forward)
    const int64_t t2_1 = (int64_t)r1.t2_sec * kNs + r1.t2_nsec;
    const int64_t t2_2 = (int64_t)r2.t2_sec * kNs + r2.t2_nsec;
    EXPECT_LE(t2_1, t2_2);
}

/* Zero-payload (all-zeros) request is a valid packet; reply must still echo t1. */
TEST_F(ServerTest, AllZeroRequestIsHandledCleanly)
{
    uint8_t zeros[NTPLITE_PACKET_SIZE] = {};
    ASSERT_EQ(ntplite_server_on_request(&srv, zeros, sizeof(zeros)), NTPLITE_OK);
    ASSERT_EQ(inbox.size(), 1u);

    const ntplite_packet_t rep = dec(inbox.front());
    EXPECT_EQ(rep.t1_sec,  0u);
    EXPECT_EQ(rep.t1_nsec, 0u);
    EXPECT_GE(to_ns(mk(rep.t2_sec, rep.t2_nsec)), 0);   // server time must be non-negative
}