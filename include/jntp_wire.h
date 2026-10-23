#ifndef JNTP_WIRE_H
#define JNTP_WIRE_H

/*
 * Private wire-format constants shared by jntp_server.c and jntp_client.c.
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

#include "jntp_types.h"

#define JNTP_WIRE_VERSION       1u

#define JNTP_WIRE_MODE_REQUEST  1u
#define JNTP_WIRE_MODE_RESPONSE 2u

#define JNTP_WIRE_OFF_VERSION   0u
#define JNTP_WIRE_OFF_MODE      1u
#define JNTP_WIRE_OFF_T1        4u
#define JNTP_WIRE_OFF_T2        12u
#define JNTP_WIRE_OFF_T3        20u

#define JNTP_WIRE_TS_SIZE       8u
#define JNTP_NSEC_PER_SEC       1000000000u

/* Compile-time check (C99 has no static_assert): layout must match packet size. */
typedef char jntp_wire_size_check[
    (JNTP_WIRE_OFF_T3 + JNTP_WIRE_TS_SIZE == JNTP_PACKET_SIZE) ? 1 : -1];

#endif /* JNTP_WIRE_H */
