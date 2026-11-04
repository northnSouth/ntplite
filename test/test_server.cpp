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