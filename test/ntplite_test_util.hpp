#pragma once

#include "ntplite_client.h"
#include "ntplite_server.h"
#include "ntplite_internal.h"   /* ntplite_packet_t, encode/decode, ntplite_offset */

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <vector>

constexpr int64_t kNs = 1000000000;
constexpr int64_t kMs = 1000000;

inline int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

inline ntplite_time_t mk(int32_t sec, uint32_t nsec)
{
    ntplite_time_t t;
    t.sec  = sec;
    t.nsec = nsec;
    return t;
}

/* int64 ns -> {sec, nsec} with the floor convention (test code only). */
inline ntplite_time_t to_ts(int64_t ns)
{
    const int64_t s = floor_div(ns, kNs);
    return mk(static_cast<int32_t>(s), static_cast<uint32_t>(ns - s * kNs));
}

inline int64_t to_ns(ntplite_time_t t)
{
    return static_cast<int64_t>(t.sec) * kNs + static_cast<int64_t>(t.nsec);
}

inline ::testing::AssertionResult TimeIs(ntplite_time_t got, int32_t sec, uint32_t nsec)
{
    if (got.sec == sec && got.nsec == nsec) return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "got {" << got.sec << ", " << got.nsec
           << "}, want {" << sec << ", " << nsec << "}";
}

inline ntplite_slew_cfg_t default_cfg()
{
    ntplite_slew_cfg_t c;
    c.slew_shift     = 11;                  /* ~488 ppm */
    c.step_threshold = mk(0, 128000000u);   /* 128 ms   */
    c.max_delta      = mk(0, 0);            /* disabled */
    return c;
}

/*
 * Simulated world shared by client and server.
 *
 *   base    true time in ns. The client clock reads base; the server clock
 *           reads base + offset. Clocks only move when a test (or a path
 *           delay below) advances base, so every number is deterministic.
 *   fwd     client -> server delay, added when the client transmits
 *   back    server -> client delay, added when the server sends
 *   offset  server minus client
 *
 * With zero server processing time: t2 == t3, delta == fwd + back and
 * theta == offset + (fwd - back) / 2.
 */
class NtpLiteSim : public ::testing::Test {
 public:
    using Packet = std::array<uint8_t, NTPLITE_PACKET_SIZE>;

    int64_t base     = 1000 * kNs;
    int64_t offset   = 0;
    int64_t fwd      = kMs;
    int64_t back     = kMs;
    int64_t srv_skew = 0;   /* added to the server's 2nd clock read (t3) */

    bool cli_bad_clock  = false;
    bool srv_bad_clock  = false;
    bool srv_send_fail  = false;
    bool hold_replies   = false;   /* replies go to `held`, not `inbox` */

    int sent      = 0;   /* requests handed to the transport */
    int srv_reads = 0;

    /* When set, the client's request goes here instead of the real server. */
    std::function<void(const uint8_t *)> override_server;

    std::deque<Packet>  inbox;   /* replies waiting for the client */
    std::vector<Packet> held;

    ntplite_slew_cfg_t   cfg = default_cfg();
    ntplite_server_ctx_t srv{};
    ntplite_client_ctx_t cli{};

    void SetUp() override
    {
        init_server();
        init_client();
    }

    void init_server()
    {
        ntplite_server_platform_t p{};
        p.time_fn  = srv_time;
        p.send_fn  = srv_send;
        p.user_arg = this;
        ASSERT_EQ(ntplite_server_init(&srv, p), NTPLITE_OK);
    }

    void init_client()
    {
        ntplite_client_platform_t p{};
        p.time_fn  = cli_time;
        p.user_arg = this;
        ASSERT_EQ(ntplite_client_init(&cli, p, cfg), NTPLITE_OK);
    }

    void advance(int64_t ns) { base += ns; }

    /* The client's request crosses the link (fwd delay) and the server answers. */
    void transmit(const uint8_t *b, size_t n)
    {
        sent++;
        base += fwd;
        srv_reads = 0;
        if (override_server) {
            override_server(b);
            return;
        }
        ntplite_server_on_request(&srv, b, n);
    }

    /*
     * One exchange, driven the way firmware drives the async API: stamp t1, begin, transmit,
     * then hand every queued reply to on_reply with its arrival time until one is not stale.
     * An empty inbox is a timeout: abort and report NTPLITE_ERR_TRANSPORT.
     */
    ntplite_result_t sync()
    {
        ntplite_result_t rc = ntplite_client_begin(&cli, cli_time(this));
        if (rc != NTPLITE_OK) return rc;

        const ntplite_span_t tx = ntplite_client_tx(&cli);
        transmit(tx.data, tx.len);

        while (!inbox.empty()) {
            const Packet p = inbox.front();
            inbox.pop_front();
            rc = ntplite_client_on_reply(&cli, p.data(), p.size(), cli_time(this));
            if (rc != NTPLITE_ERR_MISMATCH) return rc;
        }
        ntplite_client_abort(&cli);
        return NTPLITE_ERR_TRANSPORT;
    }

    /* Set the server offset, let `gap` ns pass, then run one sync. */
    ntplite_result_t sync_at(int64_t off, int64_t gap)
    {
        offset = off;
        advance(gap);
        return sync();
    }

    /* Offset currently applied to the client clock (no time passes). */
    int64_t now_offset() { return to_ns(ntplite_now(&cli)) - base; }

    static Packet enc(const ntplite_packet_t &p)
    {
        Packet k{};
        EXPECT_EQ(ntplite_packet_encode(&p, k.data(), k.size()), NTPLITE_OK);
        return k;
    }

    static ntplite_packet_t dec(const Packet &k)
    {
        ntplite_packet_t p{};
        EXPECT_EQ(ntplite_packet_decode(k.data(), k.size(), &p), NTPLITE_OK);
        return p;
    }

    /* Queue `n` late replies from earlier exchanges (t1 never matches). */
    void push_stale(int n)
    {
        for (int i = 0; i < n; i++) {
            ntplite_packet_t p{};
            p.t1_sec = static_cast<uint32_t>(1 + i);
            p.t2_sec = 2;
            p.t3_sec = 2;
            inbox.push_back(enc(p));
        }
    }

    /* ---- hooks (user_arg is the fixture) ---- */
    static NtpLiteSim *self(void *a) { return static_cast<NtpLiteSim *>(a); }

    static ntplite_time_t cli_time(void *a)
    {
        NtpLiteSim *s = self(a);
        if (s->cli_bad_clock) return mk(0, NTPLITE_NSEC_PER_SEC);
        return to_ts(s->base);
    }

    static ntplite_time_t srv_time(void *a)
    {
        NtpLiteSim *s = self(a);
        if (s->srv_bad_clock) return mk(0, NTPLITE_NSEC_PER_SEC);
        int64_t v = s->base + s->offset;
        if (++s->srv_reads == 2) v += s->srv_skew;
        return to_ts(v);
    }

    static ntplite_result_t srv_send(const uint8_t *b, size_t n, void *a)
    {
        NtpLiteSim *s = self(a);
        if (s->srv_send_fail) return NTPLITE_ERR_TRANSPORT;
        s->base += s->back;
        Packet p{};
        std::memcpy(p.data(), b, n);
        if (s->hold_replies) s->held.push_back(p);
        else                 s->inbox.push_back(p);
        return NTPLITE_OK;
    }
};