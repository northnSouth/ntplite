#include "ntplite_test_util.hpp"

namespace {
ntplite_time_t dummy_time(void *) { return mk(1, 0); }
ntplite_result_t dummy_send(const uint8_t *, size_t, void *) { return NTPLITE_OK; }
ntplite_result_t dummy_recv(uint8_t *, size_t, void *) { return NTPLITE_OK; }

ntplite_client_platform_t good_platform()
{
    ntplite_client_platform_t p{};
    p.time_fn = dummy_time;
    p.send_fn = dummy_send;
    p.recv_fn = dummy_recv;
    return p;
}
}

/* ------------------------------------------------------------------------- */
/* Init                                                                      */
/* ------------------------------------------------------------------------- */

TEST(ClientInit, AcceptsValidConfig)
{
    ntplite_client_ctx_t c;
    EXPECT_EQ(ntplite_client_init(&c, good_platform(), default_cfg()), NTPLITE_OK);
}

TEST(ClientInit, RejectsNullContextAndMissingHooks)
{
    ntplite_client_ctx_t c;
    EXPECT_EQ(ntplite_client_init(nullptr, good_platform(), default_cfg()),
              NTPLITE_ERR_INVALID);

    ntplite_client_platform_t p = good_platform();
    p.time_fn = nullptr;
    EXPECT_EQ(ntplite_client_init(&c, p, default_cfg()), NTPLITE_ERR_INVALID);

    p = good_platform();
    p.send_fn = nullptr;
    EXPECT_EQ(ntplite_client_init(&c, p, default_cfg()), NTPLITE_ERR_INVALID);

    p = good_platform();
    p.recv_fn = nullptr;
    EXPECT_EQ(ntplite_client_init(&c, p, default_cfg()), NTPLITE_ERR_INVALID);
}

TEST(ClientInit, RejectsBadSlewShift)
{
    ntplite_client_ctx_t c;
    for (uint8_t shift : { uint8_t(0), uint8_t(31), uint8_t(255) }) {
        ntplite_slew_cfg_t cfg = default_cfg();
        cfg.slew_shift = shift;
        EXPECT_EQ(ntplite_client_init(&c, good_platform(), cfg), NTPLITE_ERR_INVALID)
            << "shift " << int(shift);
    }
    for (uint8_t shift : { uint8_t(1), uint8_t(30) }) {
        ntplite_slew_cfg_t cfg = default_cfg();
        cfg.slew_shift = shift;
        EXPECT_EQ(ntplite_client_init(&c, good_platform(), cfg), NTPLITE_OK)
            << "shift " << int(shift);
    }
}

TEST(ClientInit, RejectsBadThresholds)
{
    ntplite_client_ctx_t c;
    ntplite_slew_cfg_t cfg;

    cfg = default_cfg();  cfg.step_threshold = mk(0, 0);
    EXPECT_EQ(ntplite_client_init(&c, good_platform(), cfg), NTPLITE_ERR_INVALID);

    cfg = default_cfg();  cfg.step_threshold = mk(0, 1000000000u);
    EXPECT_EQ(ntplite_client_init(&c, good_platform(), cfg), NTPLITE_ERR_INVALID);

    cfg = default_cfg();  cfg.step_threshold = mk(-1, 0);
    EXPECT_EQ(ntplite_client_init(&c, good_platform(), cfg), NTPLITE_ERR_INVALID);

    cfg = default_cfg();  cfg.max_delta = mk(0, 1000000000u);
    EXPECT_EQ(ntplite_client_init(&c, good_platform(), cfg), NTPLITE_ERR_INVALID);

    cfg = default_cfg();  cfg.max_delta = mk(5, 0);
    EXPECT_EQ(ntplite_client_init(&c, good_platform(), cfg), NTPLITE_OK);
}

TEST(ClientInit, ResetsContext)
{
    ntplite_client_ctx_t c;
    std::memset(&c, 0xFF, sizeof c);
    ASSERT_EQ(ntplite_client_init(&c, good_platform(), default_cfg()), NTPLITE_OK);
    EXPECT_FALSE(ntplite_client_is_synced(&c));
    EXPECT_TRUE(TimeIs(c.theta, 0, 0));
    EXPECT_TRUE(TimeIs(c.applied, 0, 0));
}

TEST(ClientApi, NullContextIsHandled)
{
    EXPECT_EQ(ntplite_client_sync(nullptr), NTPLITE_ERR_INVALID);
    EXPECT_TRUE(TimeIs(ntplite_now(nullptr), 0, 0));
    EXPECT_FALSE(ntplite_client_is_synced(nullptr));
}

/* ------------------------------------------------------------------------- */
/* Sync                                                                      */
/* ------------------------------------------------------------------------- */

using ClientTest = NtpLiteSim;

TEST_F(ClientTest, NowReturnsRawClockBeforeFirstSync)
{
    EXPECT_FALSE(ntplite_client_is_synced(&cli));
    EXPECT_EQ(to_ns(ntplite_now(&cli)), base);
}

TEST_F(ClientTest, FirstSyncStepsToPositiveOffset)
{
    ASSERT_EQ(sync_at(5 * kNs, 0), NTPLITE_OK);
    EXPECT_TRUE(ntplite_client_is_synced(&cli));
    EXPECT_EQ(now_offset(), 5 * kNs);
}

TEST_F(ClientTest, FirstSyncStepsToNegativeOffset)
{
    ASSERT_EQ(sync_at(-2 * kNs - 500000000, 0), NTPLITE_OK);
    EXPECT_EQ(now_offset(), -2 * kNs - 500000000);
}

TEST_F(ClientTest, AsymmetricPathErrorIsHalfTheAsymmetry)
{
    fwd = 3 * kMs;
    back = 1 * kMs;
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    EXPECT_EQ(now_offset(), (fwd - back) / 2);   /* classic NTP limitation */
}

TEST_F(ClientTest, NegativeRoundTripIsRejected)
{
    srv_skew = 10 * kMs;   /* server claims far more processing than the RTT */
    EXPECT_EQ(sync(), NTPLITE_ERR_NEGATIVE_RTT);
    EXPECT_FALSE(ntplite_client_is_synced(&cli));
}

TEST_F(ClientTest, InvalidClockRejectedBeforeSending)
{
    cli_bad_clock = true;
    EXPECT_EQ(sync(), NTPLITE_ERR_INVALID);
    EXPECT_EQ(sent, 0);
}

TEST_F(ClientTest, NowReturnsInvalidRawClockUntouched)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    const int64_t last = to_ns(cli.last_raw);
    const int64_t app  = to_ns(cli.applied);

    cli_bad_clock = true;
    const ntplite_time_t r = ntplite_now(&cli);
    cli_bad_clock = false;

    EXPECT_EQ(r.nsec, NTPLITE_NSEC_PER_SEC);
    EXPECT_EQ(to_ns(cli.last_raw), last);
    EXPECT_EQ(to_ns(cli.applied), app);
}

TEST_F(ClientTest, SendFailureIsReported)
{
    cli_send_fail = true;
    EXPECT_EQ(sync(), NTPLITE_ERR_TRANSPORT);
    EXPECT_FALSE(ntplite_client_is_synced(&cli));
}

TEST_F(ClientTest, ReplyFieldsOutOfRangeAreRejected)
{
    struct BadReply { uint32_t t2s, t2n, t3s, t3n; };
    const BadReply cases[] = {
        { 0x80000000u, 0, 1000, 0 },            /* t2 seconds beyond int32  */
        { 1000, 1000000000u, 1000, 0 },         /* t2 nsec not normalized   */
        { 1000, 0, 0x80000000u, 0 },            /* t3 seconds beyond int32  */
        { 1000, 0, 1000, 1000000000u },         /* t3 nsec not normalized   */
    };

    for (const BadReply &c : cases) {
        override_server = [this, c](const uint8_t *req) {
            ntplite_packet_t p{};
            ntplite_packet_decode(req, NTPLITE_PACKET_SIZE, &p);
            p.t2_sec = c.t2s;  p.t2_nsec = c.t2n;
            p.t3_sec = c.t3s;  p.t3_nsec = c.t3n;
            base += back;
            inbox.push_back(enc(p));
        };
        advance(kNs);
        EXPECT_EQ(sync(), NTPLITE_ERR_INVALID);
        EXPECT_FALSE(ntplite_client_is_synced(&cli));
    }
}

/* ---- max_delta filter ---- */

TEST_F(ClientTest, DelayAboveMaxIsDiscarded)
{
    cfg.max_delta = mk(0, 1000000u);   /* 1 ms; the sim's delta is 2 ms */
    init_client();
    EXPECT_EQ(sync(), NTPLITE_ERR_DELAY);
    EXPECT_FALSE(ntplite_client_is_synced(&cli));
}

TEST_F(ClientTest, DelayEqualToMaxIsAccepted)
{
    cfg.max_delta = mk(0, 2000000u);
    init_client();
    EXPECT_EQ(sync(), NTPLITE_OK);
}

TEST_F(ClientTest, ZeroMaxDeltaDisablesTheFilter)
{
    fwd = back = 500 * kMs;
    EXPECT_EQ(sync(), NTPLITE_OK);
}

TEST_F(ClientTest, FailedSyncLeavesStateUntouched)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(50 * kMs, kNs), NTPLITE_OK);   /* slew now pending */

    const int64_t theta   = to_ns(cli.theta);
    const int64_t applied = to_ns(cli.applied);
    const int64_t last    = to_ns(cli.last_raw);

    cli.slew_cfg.max_delta = mk(0, 1);   /* reject everything */
    EXPECT_EQ(sync_at(0, kNs), NTPLITE_ERR_DELAY);

    EXPECT_EQ(to_ns(cli.theta), theta);
    EXPECT_EQ(to_ns(cli.applied), applied);
    EXPECT_EQ(to_ns(cli.last_raw), last);
    EXPECT_TRUE(ntplite_client_is_synced(&cli));
}

/* ---- stale replies and timeouts ---- */

TEST_F(ClientTest, StaleRepliesBelowTheLimitAreSkipped)
{
    push_stale(static_cast<int>(NTPLITE_MAX_RECV_ATTEMPTS) - 1);
    EXPECT_EQ(sync(), NTPLITE_OK);
    EXPECT_TRUE(inbox.empty());
}

TEST_F(ClientTest, StaleRepliesAtTheLimitGiveMismatch)
{
    push_stale(static_cast<int>(NTPLITE_MAX_RECV_ATTEMPTS));
    EXPECT_EQ(sync(), NTPLITE_ERR_MISMATCH);
    EXPECT_FALSE(ntplite_client_is_synced(&cli));
    EXPECT_EQ(inbox.size(), 1u);   /* the real reply is still queued */
}

TEST_F(ClientTest, RecoversAfterMismatchByDrainingTheBacklog)
{
    if (NTPLITE_MAX_RECV_ATTEMPTS < 2) GTEST_SKIP() << "needs >= 2 recv attempts";

    push_stale(static_cast<int>(NTPLITE_MAX_RECV_ATTEMPTS));
    ASSERT_EQ(sync(), NTPLITE_ERR_MISMATCH);

    advance(kNs);
    EXPECT_EQ(sync(), NTPLITE_OK);   /* skips the leftover reply, matches the new one */
    EXPECT_TRUE(ntplite_client_is_synced(&cli));
}

TEST_F(ClientTest, TimeoutThenLateReplyDoesNotDesyncTheClient)
{
    if (NTPLITE_MAX_RECV_ATTEMPTS < 2) GTEST_SKIP() << "needs >= 2 recv attempts";

    hold_replies = true;
    ASSERT_EQ(sync(), NTPLITE_ERR_TRANSPORT);   /* reply is delayed: recv times out */
    ASSERT_EQ(held.size(), 1u);

    hold_replies = false;
    inbox.push_back(held.front());              /* the late reply finally arrives */
    held.clear();

    advance(kNs);
    EXPECT_EQ(sync(), NTPLITE_OK);              /* skipped the late one, matched ours */
    EXPECT_TRUE(ntplite_client_is_synced(&cli));
}

/* ------------------------------------------------------------------------- */
/* Slew / step                                                               */
/* ------------------------------------------------------------------------- */

using SlewTest = NtpLiteSim;

/* Advance in 10 s steps; the clock must stay strictly monotonic and the
 * applied offset must approach `target` without overshooting. */
static void expect_slews_to(NtpLiteSim &s, int64_t target)
{
    int64_t prev_time = to_ns(ntplite_now(&s.cli));
    int64_t prev_off  = prev_time - s.base;
    bool done = false;

    for (int i = 0; i < 200 && !done; i++) {
        s.advance(10 * kNs);
        const int64_t t   = to_ns(ntplite_now(&s.cli));
        const int64_t off = t - s.base;

        ASSERT_GT(t, prev_time) << "step " << i;
        if (target > 0) { ASSERT_GE(off, prev_off); ASSERT_LE(off, target); }
        else            { ASSERT_LE(off, prev_off); ASSERT_GE(off, target); }

        prev_time = t;
        prev_off  = off;
        done = (off == target);
    }
    EXPECT_TRUE(done) << "never converged";
}

TEST_F(SlewTest, SmallErrorSlewsAtTheConfiguredRate)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(50 * kMs, kNs), NTPLITE_OK);   /* below the 128 ms threshold */

    EXPECT_EQ(now_offset(), 0);                      /* no time has passed yet */

    advance(kNs);
    EXPECT_EQ(now_offset(), kNs >> 11);              /* 1 s * 2^-11 */
}

TEST_F(SlewTest, PositiveErrorConvergesMonotonically)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(50 * kMs, kNs), NTPLITE_OK);
    ASSERT_NO_FATAL_FAILURE(expect_slews_to(*this, 50 * kMs));
}

TEST_F(SlewTest, NegativeErrorConvergesMonotonically)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(-50 * kMs, kNs), NTPLITE_OK);
    ASSERT_NO_FATAL_FAILURE(expect_slews_to(*this, -50 * kMs));
}

TEST_F(SlewTest, ErrorAtOrAboveThresholdSteps)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(kNs, kNs), NTPLITE_OK);
    EXPECT_EQ(now_offset(), kNs);
}

TEST_F(SlewTest, ErrorExactlyAtThresholdSteps)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(128 * kMs, kNs), NTPLITE_OK);
    EXPECT_EQ(now_offset(), 128 * kMs);
}

TEST_F(SlewTest, ErrorJustBelowThresholdSlews)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(128 * kMs - 1, kNs), NTPLITE_OK);
    EXPECT_EQ(now_offset(), 0);
}

TEST_F(SlewTest, NegativeStep)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(-kNs, kNs), NTPLITE_OK);
    EXPECT_EQ(now_offset(), -kNs);
}

/* Regression: sync used to overwrite last_raw, discarding the slew budget
 * accumulated since the last ntplite_now() call. */
TEST_F(SlewTest, SyncAppliesPendingSlewBeforeReplacingTheTarget)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(50 * kMs, kNs), NTPLITE_OK);
    const int64_t t4_prev = base;                    /* clock reads stop at t4 */

    ASSERT_EQ(sync_at(50 * kMs, 10 * kNs), NTPLITE_OK);   /* no now() in between */
    const int64_t expected = (base - t4_prev) >> 11;

    EXPECT_GT(expected, 0);
    EXPECT_EQ(now_offset(), expected);
}

/* Regression: the step decision used to compare the new theta against an
 * `applied` that had not yet absorbed the pending slew, causing a spurious
 * step when the old slew would already have closed the gap. */
TEST_F(SlewTest, PendingSlewClosingTheGapPreventsSpuriousStep)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(100 * kMs, kNs), NTPLITE_OK);        /* slew to +100 ms pending */

    /* 300 s without ntplite_now(): enough to finish the 100 ms slew. Then the
     * server moves another 100 ms. True error is 100 ms (< 128 ms): slew. A
     * stale `applied` would see 200 ms and step. */
    ASSERT_EQ(sync_at(200 * kMs, 300 * kNs), NTPLITE_OK);
    EXPECT_EQ(now_offset(), 100 * kMs);
}

TEST_F(SlewTest, RawClockGoingBackwardsResetsTheSlewReference)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(50 * kMs, kNs), NTPLITE_OK);
    EXPECT_EQ(now_offset(), 0);

    advance(-kNs / 2);                               /* raw clock jumps back 0.5 s */
    EXPECT_EQ(now_offset(), 0);                      /* no slew, no crash */

    advance(kNs);                                    /* exactly 1 s after the reset */
    EXPECT_EQ(now_offset(), kNs >> 11);
}

// -------------------------------------------------------------------------
// Slew precision and overshoot
// -------------------------------------------------------------------------

/* Slew advance for exactly one elapsed unit must equal elapsed >> slew_shift,
 * no more, no less. */
TEST_F(SlewTest, SlewRateMatchesShiftExactly)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(50 * kMs, kNs), NTPLITE_OK);

    // 1024 ms elapsed; shift=11 → budget = 1024ms >> 11 = 0.5ms exactly
    const int64_t elapsed = 1024 * kMs;
    advance(elapsed);
    EXPECT_EQ(now_offset(), elapsed >> 11);
}

/* Slew budget from multiple now() calls must add up to the same total as
 * one single call covering the same elapsed time. */
TEST_F(SlewTest, SlewBudgetAccumulatesCorrectlyAcrossNowCalls)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(50 * kMs, kNs), NTPLITE_OK);

    advance(512 * kMs); ntplite_now(&cli);
    advance(512 * kMs);
    EXPECT_EQ(now_offset(), 1024 * kMs >> 11);
}

/* Calling now() repeatedly without any time passing must be idempotent. */
TEST_F(SlewTest, RepeatedNowWithNoElapsedTimeIsIdempotent)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(50 * kMs, kNs), NTPLITE_OK);
    advance(kNs);

    const int64_t first = now_offset();
    for (int i = 0; i < 20; i++)
        EXPECT_EQ(now_offset(), first) << "changed on call " << i;
}

/* applied must never exceed theta (positive target). */
TEST_F(SlewTest, SlewNeverOvershootsPositiveTarget)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(50 * kMs, kNs), NTPLITE_OK);
    const int64_t target = 50 * kMs;

    for (int i = 0; i < 500; i++) {
        advance(kNs);
        const int64_t off = now_offset();
        ASSERT_LE(off, target) << "overshot at step " << i;
        ASSERT_GE(off, 0)      << "went below zero at step " << i;
        if (off == target) return;
    }
    FAIL() << "never converged";
}

/* applied must never go below theta (negative target). */
TEST_F(SlewTest, SlewNeverOvershootsNegativeTarget)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(-50 * kMs, kNs), NTPLITE_OK);
    const int64_t target = -50 * kMs;

    for (int i = 0; i < 500; i++) {
        advance(kNs);
        const int64_t off = now_offset();
        ASSERT_GE(off, target) << "overshot at step " << i;
        ASSERT_LE(off, 0)      << "went above zero at step " << i;
        if (off == target) return;
    }
    FAIL() << "never converged";
}

/* Zero elapsed time between two now() calls must produce zero slew budget
 * (last_raw unchanged → budget is 0 → applied unchanged). */
TEST_F(SlewTest, ZeroElapsedProducesZeroBudget)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(50 * kMs, kNs), NTPLITE_OK);

    // No advance before first now(): budget must be zero.
    EXPECT_EQ(now_offset(), 0);
}

// -------------------------------------------------------------------------
// Error-path state preservation
// -------------------------------------------------------------------------

/* A send failure while already synced must not touch theta, applied, or
 * last_raw (error-path state invariant). */
TEST_F(ClientTest, SendFailureAfterSyncLeavesStateUntouched)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(50 * kMs, kNs), NTPLITE_OK);

    const int64_t theta   = to_ns(cli.theta);
    const int64_t applied = to_ns(cli.applied);
    const int64_t last    = to_ns(cli.last_raw);

    cli_send_fail = true;
    EXPECT_EQ(sync(), NTPLITE_ERR_TRANSPORT);
    cli_send_fail = false;

    EXPECT_EQ(to_ns(cli.theta),   theta);
    EXPECT_EQ(to_ns(cli.applied), applied);
    EXPECT_EQ(to_ns(cli.last_raw), last);
    EXPECT_TRUE(ntplite_client_is_synced(&cli));
}

/* An inbox that runs dry (timeout) before any matching reply must not
 * touch client state either. */
TEST_F(ClientTest, TimeoutFromEmptyInboxLeavesStateUntouched)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);

    const int64_t theta   = to_ns(cli.theta);
    const int64_t applied = to_ns(cli.applied);
    const int64_t last    = to_ns(cli.last_raw);

    // Server sends nothing.
    override_server = [this](const uint8_t *) { base += back; };
    EXPECT_EQ(sync(), NTPLITE_ERR_TRANSPORT);
    override_server = nullptr;

    EXPECT_EQ(to_ns(cli.theta),    theta);
    EXPECT_EQ(to_ns(cli.applied),  applied);
    EXPECT_EQ(to_ns(cli.last_raw), last);
    EXPECT_TRUE(ntplite_client_is_synced(&cli));
}

// -------------------------------------------------------------------------
// Step vs slew boundary: exact threshold
// -------------------------------------------------------------------------

/* One nanosecond below the threshold must slew (applied == 0 immediately). */
TEST_F(SlewTest, OneNsBelowThresholdSlews)
{
    const int64_t just_below = to_ns(cfg.step_threshold) - 1;
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(just_below, kNs), NTPLITE_OK);
    EXPECT_EQ(now_offset(), 0);   // no time has passed yet → applied still 0
}

/* One nanosecond above the threshold must step (applied == theta immediately). */
TEST_F(SlewTest, OneNsAboveThresholdSteps)
{
    const int64_t just_above = to_ns(cfg.step_threshold) + 1;
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(just_above, kNs), NTPLITE_OK);
    EXPECT_EQ(now_offset(), just_above);
}

// -------------------------------------------------------------------------
// now() output is monotonically non-decreasing (time must not go backwards)
// -------------------------------------------------------------------------

TEST_F(ClientTest, NowOutputIsStrictlyNonDecreasing)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(50 * kMs, kNs), NTPLITE_OK);

    int64_t prev = to_ns(ntplite_now(&cli));
    for (int i = 0; i < 200; i++) {
        advance(10 * kNs);
        const int64_t cur = to_ns(ntplite_now(&cli));
        ASSERT_GE(cur, prev) << "time went backwards at step " << i;
        prev = cur;
    }
}