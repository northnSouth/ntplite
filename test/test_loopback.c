#include <stdio.h>
#include "ntplite_client.h"
#include "ntplite_server.h"

typedef struct { uint64_t ns; uint64_t step; int64_t skew; } fclock_t;
static int fnow(void *c, ntplite_timestamp_t *o)
{
    fclock_t *f = c;
    uint64_t v = (uint64_t)((int64_t)f->ns + f->skew);
    o->sec = (uint32_t)(v / 1000000000u);
    o->nsec = (uint32_t)(v % 1000000000u);
    f->ns += f->step;
    return 0;
}

typedef struct { ntplite_server_t *srv; uint8_t r[ntplite_PACKET_SIZE]; size_t n; int fail; } loop_t;
static int lsend(void *c, const uint8_t *b, size_t n)
{
    loop_t *l = c;
    if (l->fail > 0) { l->fail--; return 1; }
    return ntplite_server_handle(l->srv, b, n, l->r, sizeof l->r, &l->n) != ntplite_OK;
}
static int lrecv(void *c, uint8_t *b, size_t cap, size_t *len, uint32_t to)
{
    loop_t *l = c; size_t i; (void)to;
    if (l->n > cap) return 1;
    for (i = 0; i < l->n; i++) b[i] = l->r[i];
    *len = l->n;
    return 0;
}

int main(void)
{
    static fclock_t cc = { 100000000000ull, 1000000, 0 };
    static fclock_t sc = { 100000000000ull, 1000000, 5000000000ll }; /* +5 s */
    static ntplite_server_t srv;
    static ntplite_client_t cli;
    static loop_t lp;
    ntplite_clock_t cclk = { fnow, &cc }, sclk = { fnow, &sc };
    ntplite_transport_t tr = { lsend, lrecv, &lp };
    ntplite_result_t r;
    int rc;

    lp.srv = &srv;
    ntplite_server_init(&srv, &sclk);
    ntplite_client_init(&cli, &tr, &cclk, 3, 100);

    lp.fail = 2; /* two failures, third attempt succeeds */
    rc = ntplite_client_sync(&cli, &r);
    printf("retry ok:   rc=%d offset=%lld ns delay=%lld ns\n", rc,
           (long long)r.offset_ns, (long long)r.delay_ns);

    lp.fail = 3; /* exhausts attempts */
    rc = ntplite_client_sync(&cli, &r);
    printf("exhausted:  rc=%d (expect %d)\n", rc, ntplite_ERR_TRANSPORT);
    return 0;
}
