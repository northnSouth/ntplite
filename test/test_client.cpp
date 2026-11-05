#include "ntplite_test_util.hpp"

namespace {
ntplite_time_t dummy_time(void *) { return mk(1, 0); }

ntplite_client_platform_t good_platform()
{
    ntplite_client_platform_t p{};
    p.time_fn = dummy_time;
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

TEST(ClientInit, RejectsNullContextAndMissingClock)
{
    ntplite_client_ctx_t c;
    EXPECT_EQ(ntplite_client_init(nullptr, good_platform(), default_cfg()), NTPLITE_ERR_INVALID);

    ntplite_client_platform_t p = good_platform();
    p.time_fn = nullptr;
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
    EXPECT_FALSE(ntplite_client_pending(&c));
    EXPECT_EQ(ntplite_client_tx(&c).len, 0u);
    EXPECT_TRUE(TimeIs(c.theta, 0, 0));
    EXPECT_TRUE(TimeIs(c.applied, 0, 0));
    EXPECT_EQ(c.debug.syncs_ok, 0u);
}

TEST(ClientApi, NullContextIsHandled)
{
    const uint8_t b[NTPLITE_PACKET_SIZE] = {};
    EXPECT_EQ(ntplite_client_begin(nullptr, mk(1, 0)), NTPLITE_ERR_INVALID);
    EXPECT_EQ(ntplite_client_on_reply(nullptr, b, sizeof b, mk(1, 0)), NTPLITE_ERR_INVALID);
    EXPECT_EQ(ntplite_client_tx(nullptr).len, 0u);
    EXPECT_FALSE(ntplite_client_pending(nullptr));
    ntplite_client_abort(nullptr);
    EXPECT_TRUE(TimeIs(ntplite_now(nullptr), 0, 0));
    EXPECT_FALSE(ntplite_client_is_synced(nullptr));
}

/* ------------------------------------------------------------------------- */
/* Request / reply                                                           */
/* ------------------------------------------------------------------------- */

using ClientTest = NtpLiteSim;

TEST_F(ClientTest, BeginWritesARequestCarryingT1)
{
    const ntplite_time_t t1 = to_ts(base);
    ASSERT_EQ(ntplite_client_begin(&cli, t1), NTPLITE_OK);
    EXPECT_TRUE(ntplite_client_pending(&cli));

    const ntplite_span_t tx = ntplite_client_tx(&cli);
    ASSERT_EQ(tx.len, NTPLITE_PACKET_SIZE);
    Packet p{};
    std::memcpy(p.data(), tx.data, tx.len);
    const ntplite_packet_t req = dec(p);
    EXPECT_EQ(req.t1_sec, static_cast<uint32_t>(t1.sec));
    EXPECT_EQ(req.t1_nsec, t1.nsec);
}

TEST_F(ClientTest, RequestsMustNotRepeatT1)
{
    ASSERT_EQ(ntplite_client_begin(&cli, mk(5, 0)), NTPLITE_OK);
    EXPECT_EQ(ntplite_client_begin(&cli, mk(5, 0)), NTPLITE_ERR_INVALID);
    EXPECT_EQ(ntplite_client_begin(&cli, mk(5, 1)), NTPLITE_OK);
}

TEST_F(ClientTest, ArrivalTimestampIsUsedNotProcessingTime)
{
    /* The reply waits 5 ms in a queue before the timekeeper task handles it. With t4 stamped
     * at arrival the offset is exact; t4 taken at processing time would bias it by 2.5 ms. */
    offset = 3 * kNs;
    ASSERT_EQ(ntplite_client_begin(&cli, to_ts(base)), NTPLITE_OK);
    const ntplite_span_t tx = ntplite_client_tx(&cli);
    transmit(tx.data, tx.len);

    const ntplite_time_t t4 = to_ts(base);   /* arrival */
    advance(5 * kMs);                        /* queueing / scheduling */
    ASSERT_EQ(inbox.size(), 1u);
    ASSERT_EQ(ntplite_client_on_reply(&cli, inbox.front().data(), NTPLITE_PACKET_SIZE, t4),
              NTPLITE_OK);
    EXPECT_EQ(to_ns(cli.theta), 3 * kNs);
}

TEST_F(ClientTest, ReplyWithNothingPendingIsStale)
{
    ASSERT_EQ(sync(), NTPLITE_OK);
    const int64_t theta = to_ns(cli.theta);

    ntplite_packet_t p{};
    p.t1_sec  = static_cast<uint32_t>(cli.req_t1.sec);   /* even the right t1 */
    p.t1_nsec = cli.req_t1.nsec;
    const Packet r = enc(p);
    EXPECT_EQ(ntplite_client_on_reply(&cli, r.data(), r.size(), to_ts(base)), NTPLITE_ERR_MISMATCH);
    EXPECT_EQ(to_ns(cli.theta), theta);
    EXPECT_EQ(cli.debug.stale_dropped, 1u);
}

TEST_F(ClientTest, ShortReplyKeepsTheRequestPending)
{
    ASSERT_EQ(ntplite_client_begin(&cli, to_ts(base)), NTPLITE_OK);
    const uint8_t junk[10] = {};
    EXPECT_EQ(ntplite_client_on_reply(&cli, junk, sizeof junk, to_ts(base)), NTPLITE_ERR_BUFFER);
    EXPECT_TRUE(ntplite_client_pending(&cli));
}

TEST_F(ClientTest, AbortEndsTheRequestAndIsCounted)
{
    ASSERT_EQ(ntplite_client_begin(&cli, to_ts(base)), NTPLITE_OK);
    ntplite_client_abort(&cli);
    EXPECT_FALSE(ntplite_client_pending(&cli));
    EXPECT_EQ(cli.debug.aborted, 1u);
    ntplite_client_abort(&cli);                 /* nothing pending: not counted */
    EXPECT_EQ(cli.debug.aborted, 1u);
}

/* ------------------------------------------------------------------------- */
/* Sync                                                                      */
/* ------------------------------------------------------------------------- */

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
    EXPECT_FALSE(ntplite_client_pending(&cli));
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
        EXPECT_FALSE(ntplite_client_pending(&cli));
    }
}

TEST_F(ClientTest, ServerFarAheadIsRejectedWithoutOverflow)
{
    offset = (static_cast<int64_t>(NTPLITE_MAX_OFFSET_SEC) + 1) * kNs;
    EXPECT_EQ(sync(), NTPLITE_ERR_INVALID);
    EXPECT_FALSE(ntplite_client_is_synced(&cli));
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

TEST_F(ClientTest, StaleRepliesAreSkipped)
{
    push_stale(10);
    EXPECT_EQ(sync(), NTPLITE_OK);
    EXPECT_TRUE(inbox.empty());
    EXPECT_EQ(cli.debug.stale_dropped, 10u);
}

TEST_F(ClientTest, TimeoutThenLateReplyDoesNotDesyncTheClient)
{
    hold_replies = true;
    ASSERT_EQ(sync(), NTPLITE_ERR_TRANSPORT);   /* no reply: the sim aborts */
    ASSERT_EQ(held.size(), 1u);
    EXPECT_EQ(cli.debug.aborted, 1u);

    hold_replies = false;
    inbox.push_back(held.front());              /* the late reply finally arrives */
    held.clear();

    advance(kNs);
    EXPECT_EQ(sync(), NTPLITE_OK);              /* skipped the late one, matched ours */
    EXPECT_EQ(cli.debug.stale_dropped, 1u);
    EXPECT_TRUE(ntplite_client_is_synced(&cli));
}

TEST_F(ClientTest, TimeoutLeavesStateUntouched)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);

    const int64_t theta   = to_ns(cli.theta);
    const int64_t applied = to_ns(cli.applied);
    const int64_t last    = to_ns(cli.last_raw);

    override_server = [this](const uint8_t *) { base += back; };   /* server stays silent */
    EXPECT_EQ(sync(), NTPLITE_ERR_TRANSPORT);
    override_server = nullptr;

    EXPECT_EQ(to_ns(cli.theta),    theta);
    EXPECT_EQ(to_ns(cli.applied),  applied);
    EXPECT_EQ(to_ns(cli.last_raw), last);
    EXPECT_TRUE(ntplite_client_is_synced(&cli));
}

/* ---- debug counters ---- */

TEST_F(ClientTest, CountersTrackOutcomes)
{
    push_stale(2);
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);              /* first sync: step */
    ASSERT_EQ(sync_at(50 * kMs, kNs), NTPLITE_OK);     /* small: slew      */
    ASSERT_EQ(sync_at(kNs, kNs), NTPLITE_OK);          /* large: step      */

    srv_skew = 10 * kMs;
    EXPECT_EQ(sync(), NTPLITE_ERR_NEGATIVE_RTT);
    srv_skew = 0;

    cli_bad_clock = true;
    EXPECT_EQ(sync(), NTPLITE_ERR_INVALID);
    cli_bad_clock = false;

    cli.slew_cfg.max_delta = mk(0, 1);
    EXPECT_EQ(sync(), NTPLITE_ERR_DELAY);

    EXPECT_EQ(cli.debug.syncs_ok,         3u);
    EXPECT_EQ(cli.debug.steps,            2u);
    EXPECT_EQ(cli.debug.stale_dropped,    2u);
    EXPECT_EQ(cli.debug.err_negative_rtt, 1u);
    EXPECT_EQ(cli.debug.err_invalid,      1u);
    EXPECT_EQ(cli.debug.err_delay,        1u);
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

TEST_F(SlewTest, SlewRateMatchesShiftExactly)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(50 * kMs, kNs), NTPLITE_OK);

    const int64_t elapsed = 1024 * kMs;              /* >> 11 = 0.5 ms exactly */
    advance(elapsed);
    EXPECT_EQ(now_offset(), elapsed >> 11);
}

TEST_F(SlewTest, SlewBudgetAccumulatesCorrectlyAcrossNowCalls)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(50 * kMs, kNs), NTPLITE_OK);

    advance(512 * kMs); ntplite_now(&cli);
    advance(512 * kMs);
    EXPECT_EQ(now_offset(), 1024 * kMs >> 11);
}

TEST_F(SlewTest, RepeatedNowWithNoElapsedTimeIsIdempotent)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(50 * kMs, kNs), NTPLITE_OK);
    advance(kNs);

    const int64_t first = now_offset();
    for (int i = 0; i < 20; i++)
        EXPECT_EQ(now_offset(), first) << "changed on call " << i;
}

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

TEST_F(SlewTest, ZeroElapsedProducesZeroBudget)
{
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(50 * kMs, kNs), NTPLITE_OK);
    EXPECT_EQ(now_offset(), 0);
}

TEST_F(SlewTest, OneNsBelowThresholdSlews)
{
    const int64_t just_below = to_ns(cfg.step_threshold) - 1;
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(just_below, kNs), NTPLITE_OK);
    EXPECT_EQ(now_offset(), 0);
}

TEST_F(SlewTest, OneNsAboveThresholdSteps)
{
    const int64_t just_above = to_ns(cfg.step_threshold) + 1;
    ASSERT_EQ(sync_at(0, 0), NTPLITE_OK);
    ASSERT_EQ(sync_at(just_above, kNs), NTPLITE_OK);
    EXPECT_EQ(now_offset(), just_above);
}

TEST_F(ClientTest, NowOutputIsNonDecreasing)
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