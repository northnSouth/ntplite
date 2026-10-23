#ifndef ntplite_WIRE_H
#define ntplite_WIRE_H

/*
 * Private wire-format constants shared by ntplite_server.c and ntplite_client.c.
 * Not part of the public API; do not include from application code.
 *
 * Layout (big endian):
 *   0      version
 *   1      mode
 *   2-3    reserved (zero)
 *   4-11   t1 {sec, nsec}  client tx, echoed by server
 *   12-19  t2 {sec, nsec}  server rx
 *   20-27  t3 {sec, nsec}  server tx
 */

#include "ntplite_types.h"

#define ntplite_WIRE_VERSION       1u

#define ntplite_WIRE_MODE_REQUEST  1u
#define ntplite_WIRE_MODE_RESPONSE 2u

#define ntplite_WIRE_OFF_VERSION   0u
#define ntplite_WIRE_OFF_MODE      1u
#define ntplite_WIRE_OFF_T1        4u
#define ntplite_WIRE_OFF_T2        12u
#define ntplite_WIRE_OFF_T3        20u

#define ntplite_WIRE_TS_SIZE       8u
#define ntplite_NSEC_PER_SEC       1000000000u

/* Compile-time check (C99 has no static_assert): layout must match packet size. */
typedef char ntplite_wire_size_check[
    (ntplite_WIRE_OFF_T3 + ntplite_WIRE_TS_SIZE == ntplite_PACKET_SIZE) ? 1 : -1];

#endif /* ntplite_WIRE_H */
