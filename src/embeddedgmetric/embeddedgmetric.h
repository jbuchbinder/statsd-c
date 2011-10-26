/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 4 -*- */
/* vi: set expandtab shiftwidth=4 tabstop=4: */

/**
 * This is the MIT LICENSE
 * http://www.opensource.org/licenses/mit-license.php
 *
 * Copyright (c) 2007 Nick Galbreath
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/** \file embbededmetric.h
 *
 * A very simple "gmetric" UDP interface
 *
 * more doco here.
 *
 * basic usage for gmond 3.1+ might be:
 * \code
 * gmetric_t g;
 * gmetric_message_t msg;
 * gmetric_create(&g);
 * if (!gmetric_open(g, "localhost", 8649)) {
 *    exit(1);
 * }
 * foreach metric {
 *    msg.hostname = "localhost";
 *    msg.spoof = 0;
 *    msg.type = GMETRIC_VALUE_STRING;
 *    msg.name = "foo";
 *    msg.group = "group";
 *    msg.value.v_string = "bar";
 *    msg.unit = "no units";
 *    msg.slope = GMETRIC_SLOPE_BOTH;
 *    msg.tmax = 120;
 *    msg.dmax = 0;
 *    gmetric_send(&g, &msg);
 * }
 * gmetric_close(g);
 * \endcode
 *
 * basic usage for gmond < 3.1 might be:
 * \code
 * gmetric_t g;
 * gmetric_message_t msg;
 * gmetric_create(&g);
 * if (!gmetric_open(g, "localhost", 8649)) {
 *    exit(1);
 * }
 * foreach metric {
 *    msg.format = GMETRIC_FORMAT_25;
 *    msg.type = GMETRIC_VALUE_STRING;
 *    msg.name = "foo";
 *    msg.value.v_string = "bar";
 *    msg.unit = "no units";
 *    msg.slope = GMETRIC_SLOPE_BOTH;
 *    msg.tmax = 120;
 *    msg.dmax = 0;
 *    gmetric_send(&g, &msg);
 * }
 * gmetric_close(g);
 * \endcode
 *
 */

#ifndef COM_MODP_EMBEDDED_GMETRIC_H
#define COM_MODP_EMBEDDED_GMETRIC_H

#include <stdint.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <rpc/rpc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GMETRIC_MAX_MESSAGE_LEN 1024

typedef enum {
    GMETRIC_VALUE_UNKNOWN = 0,
    GMETRIC_VALUE_STRING = 1,
    GMETRIC_VALUE_UNSIGNED_SHORT = 2,
    GMETRIC_VALUE_SHORT = 3,
    GMETRIC_VALUE_UNSIGNED_INT = 4,
    GMETRIC_VALUE_INT = 5,
    GMETRIC_VALUE_FLOAT = 6,
    GMETRIC_VALUE_DOUBLE = 7,
} gmetric_value_t;

typedef enum {
    GMETRIC_SLOPE_ZERO = 0,
    GMETRIC_SLOPE_POSITIVE = 1,
    GMETRIC_SLOPE_NEGATIVE = 2,
    GMETRIC_SLOPE_BOTH = 3,
    GMETRIC_SLOPE_UNSPECIFIED = 4
} gmetric_slope_t;

typedef enum {
    GMETRIC_FORMAT_25 = 0,
    GMETRIC_FORMAT_31 = 1
} gmetric_format_t;

/**
 * Control structure and shared buffers
 */
typedef struct
{
    struct sockaddr_in sa;
    int s;
} gmetric_t;

/**
 * message structure
 */
typedef struct
{
    gmetric_value_t type;
    gmetric_format_t format; /* Defaults to gmond 3.1.  Explicit set for older gmond format (2.5 to 3.0) */
    const char* hostname;    /* Only used for gmond 3.1. */
    bool_t spoof;            /* Only used for gmond 3.1. */
    const char* name;
    const char* group;       /* Only used for gmond 3.1. */
    const char* units;
    const char* typestr;
    gmetric_slope_t slope;
    uint32_t tmax;
    uint32_t dmax;
	union {
		const char*    v_string;
		unsigned short v_ushort;
		short          v_short;
		unsigned int   v_uint;
		int            v_int;
		float          v_float;
		double         v_double;
	} value;
} gmetric_message_t;

/** \brief "constructor"
 *
 */
void gmetric_create(gmetric_t* g);

/** \brief open a UDP socket
 *
 * open up a socket.  Needs to be done before calling gmetric_send.
 *
 * THIS MAY BE THREAD UNSAFE since it calls gethostbyname.
 *
 * \param[in] addr the hostname to connect to, e.g. "mygmond.com" or "127.0.0.1"
 * \param[in] port the port to connect to, in HOST order
 * \return 1 if ok, 0 if false (boolean)
 */
int gmetric_open(gmetric_t* g, const char* addr, int port);


/** \brief Raw interface to open a gmetric socket that skips gethostbyname_X
 *
 * \param[in] ip the ip address IN NETWORK ORDER
 * \param[in] port IN HOST order (e.g. "8649")
 * \return 1 if ok, 0 if false (boolean)
 */
int gmetric_open_raw(gmetric_t* g, uint32_t ip, int port);

/** \brief "destructor"
 *
 * \param[in] g the gmetric to close
 */
void gmetric_close(gmetric_t* g);

/** \brief send a metric to the socket
 *
 * Must have called gmetric_create, gmetric_open first!
 *
 * This just wraps a call around gmetadata_message_create_xdr, 
 * gmetric31_message_create_xdr, and gmetric_send_xdr for gmond 3.1,
 * and gmetric_message_create_xdr and gmetric_send_xdr for gmond 2.5.
 *
 * \param[in] g the socket to use
 * \param[in] msg the message to send
 * \return -1 on error, number of bytes sent on success
 *
 */
int gmetric_send(gmetric_t* g, const gmetric_message_t* msg);

/** \brief Send a raw XDR buffer to gmond
 *
 * "normally" you shouldn't have to use this but it may aid in making
 * optimized batch calls where one can recycle a buffer.
 *
 * \param[in] g the gmetric socket to use
 * \param[in] buf the xdr buffer
 * \param[in] len the length of data
 * \return -1 on error, number of bytes sent on success.
 */
int gmetric_send_xdr(gmetric_t* g, const char* buf, int len);

/** \brief struct to XDR message (gmond 2.5)
 *
 * Internal function but you may find it useful
 * for testing and experimenting
 *
 * \param[out] buffer
 * \param[in]  len length of buffer
 * \param[in] msg the gmetric_message to convert
 * \return -1 on error, length of message on success
 */
int gmetric_message_create_xdr(char* buffer, uint len,
                               const gmetric_message_t* msg);

/** \brief struct to XDR message (gmond 3.1)
 *
 * Internal function but you may find it useful
 * for testing and experimenting
 *
 * \param[out] buffer
 * \param[in]  len length of buffer
 * \param[in] msg the gmetric_message to convert
 * \return -1 on error, length of message on success
 */
int gmetadata_message_create_xdr(char* buffer, uint len,
                                 const gmetric_message_t* msg);

/** \brief struct to XDR message (gmond 3.1)
 *
 * Internal function but you may find it useful
 * for testing and experimenting
 *
 * \param[out] buffer
 * \param[in]  len length of buffer
 * \param[in] msg the gmetric_message to convert
 * \return -1 on error, length of message on success
 */
int gmetric31_message_create_xdr(char* buffer, uint len,
                                 const gmetric_message_t* msg);

/** \brief clear out a gmetric_message to know defaults
 *
 */
void gmetric_message_clear(gmetric_message_t* msg);

/** \brief validate a gmetric message
 *
 * \return 1 if ok, 0 if bad
 */
int gmetric_message_validate(const gmetric_message_t* msg);

#ifdef __cplusplus
}
#endif

#endif

