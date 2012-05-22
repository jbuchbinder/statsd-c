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

#include "embeddedgmetric.h"
#include "modp_numtoa.h"
#include <netdb.h>
#include <rpc/rpc.h>
#include <unistd.h>
#include <string.h>

#define CONVERT_TO_STRINGS

static const char* typestrings[] = {
    "", "string", "uint16", "int16", "uint32", "int32", "float", "double", "timestamp"
};

static const char* formatstrings[] = {
    "", "%uh", "%h", "%d", "%u", "%s", "%f", "%lf", "%s"
};
/* TODO not sure how 'timestamp' is used */

int gmetric_message_create_xdr(char* buffer, uint len,
                               const gmetric_message_t* msg)
{

    XDR x;
    xdrmem_create(&x, buffer, len, XDR_ENCODE);

    char valbuf[64];
    char* valbufptr = valbuf;

    enum_t tmp = 0;
    if (!xdr_enum (&x, (enum_t*) &tmp)) {
        return -1;
    }

    const char* typestr = typestrings[msg->type];
    if (msg->typestr && msg->typestr[0] != 0) {
        typestr = msg->typestr;
    }
    if (!xdr_string(&x, (char**) &typestr, ~0)) {
        return -1;
    }

    if (!xdr_string(&x, (char**) &msg->name, ~0)) {
        return -1;
    }

    /*
    switch (msg->type) {
    case GMETRIC_VALUE_UNSIGNED_SHORT:
        modp_uitoa10(msg->value.v_ushort, valbuf);
        if (!xdr_string(&x, &valbufptr, sizeof(valbuf))) {
            return -1;
        }
        break;
    case GMETRIC_VALUE_SHORT:
        modp_itoa10(msg->value.v_ushort, valbuf);
        if (!xdr_string(&x, &valbufptr, sizeof(valbuf))) {
            return -1;
        }
        break;
    case GMETRIC_VALUE_UNSIGNED_INT:
        modp_uitoa10(msg->value.v_uint, valbuf);
        if (!xdr_string(&x,  &valbufptr, sizeof(valbuf))) {
            return -1;
        }
        break;
    case GMETRIC_VALUE_INT:
        modp_itoa10(msg->value.v_int, valbuf);
        if (!xdr_string(&x, &valbufptr, sizeof(valbuf))) {
            return -1;
        }
        break;
    case GMETRIC_VALUE_FLOAT:
        modp_dtoa(msg->value.v_float, valbuf, 6);
        if (!xdr_string(&x,  &valbufptr, sizeof(valbuf))) {
            return -1;
        }
        break;
    case GMETRIC_VALUE_DOUBLE:
        modp_dtoa(msg->value.v_double, valbuf, 6);
        if (!xdr_string(&x, &valbufptr, sizeof(valbuf))) {
            return -1;
        }
        break;
    case GMETRIC_VALUE_STRING:
        if (!xdr_string(&x, (char**) &msg->value.v_string, ~0)) {
            return -1;
        }
        break;
    case GMETRIC_VALUE_UNKNOWN:
        if (!xdr_string(&x, (char**) &msg->value.v_string, ~0)) {
            return -1;
        }
        break;
    } */
    /* end switch */

    /* Force this to always send string */
    if (!xdr_string(&x, (char**) &msg->value.v_string, ~0)) {
        return -1;
    }

    if (!xdr_string(&x, (char**) &msg->units, ~0)) {
        return -1;
    }

    if (!xdr_u_int(&x, (u_int*) (void*) &msg->slope)) {
        return -1;
    }

    if (!xdr_u_int(&x, (u_int*) &msg->tmax)) {
        return -1;
    }

    if (!xdr_u_int(&x, (u_int*) &msg->dmax)) {
        return -1;
    }

    return xdr_getpos(&x);
}

int gmetadata_message_create_xdr(char* buffer, uint len,
                                 const gmetric_message_t* msg)
{
    enum_t tmp = 128;
    u_int tmp_int = 0;
    const char *groupStr = "GROUP";
    const char* typestr = typestrings[msg->type];
    XDR x;
    xdrmem_create(&x, buffer, len, XDR_ENCODE);

    if (!xdr_enum (&x, (enum_t*) &tmp)) {
        return -1;
    }

    if (!xdr_string(&x, (char**) &msg->hostname, ~0)) {
        return -1;
    }

    if (!xdr_string(&x, (char**) &msg->name, ~0)) {
        return -1;
    }

    if (!xdr_bool(&x, (bool_t*) &msg->spoof)) {
        return -1;
    }

    if (msg->typestr && msg->typestr[0] != 0) {
        typestr = msg->typestr;
    }
    if (!xdr_string(&x, (char**) &typestr, ~0)) {
        return -1;
    }

    if (!xdr_string(&x, (char**) &msg->name, ~0)) {
        return -1;
    }

    if (!xdr_string(&x, (char**) &msg->units, ~0)) {
        return -1;
    }

    tmp_int = (u_int) msg->slope;
    if (!xdr_u_int(&x, (u_int*) &tmp_int)) {
        return -1;
    }

    if (!xdr_u_int(&x, (u_int*) &msg->tmax)) {
        return -1;
    }

    if (!xdr_u_int(&x, (u_int*) &msg->dmax)) {
        return -1;
    }

    if (msg->group == NULL || msg->group[0] == 0) {
        tmp_int = 0;

         if (!xdr_u_int(&x, (u_int*) &tmp_int)) {
            return -1;
         }
    } else {
        tmp_int = 1;

         if (!xdr_u_int(&x, (u_int*) &tmp_int)) {
            return -1;
         }

        if (!xdr_string(&x, (char**) &groupStr, ~0)) {
            return -1;
        }

        if (!xdr_string(&x, (char**) &msg->group, ~0)) {
            return -1;
        }
    }

    return xdr_getpos(&x);
}

int gmetric31_message_create_xdr(char* buffer, uint len,
                                 const gmetric_message_t* msg)
{
    enum_t tmp = 128;
    const char* formatstr = "%s"; /* Hardcoded, which is stupid, but is part of the ganglia protocol. */
    XDR x;
    xdrmem_create(&x, buffer, len, XDR_ENCODE);

    tmp += (enum_t)msg->type;
    if (!xdr_enum (&x, (enum_t*) &tmp)) {
        return -1;
    }

    if (!xdr_string(&x, (char**) &msg->hostname, ~0)) {
        return -1;
    }

    if (!xdr_string(&x, (char**) &msg->name, ~0)) {
        return -1;
    }

    if (!xdr_bool(&x, (bool_t*) &msg->spoof)) {
        return -1;
    }

    if (!xdr_string(&x, (char**) &formatstr, ~0)) {
        return -1;
    }

    /*
    switch (msg->type) {
    case GMETRIC_VALUE_UNSIGNED_SHORT:
        if (!xdr_u_short(&x, (u_short *) &msg->value.v_ushort)) {
            return -1;
        }
        break;
    case GMETRIC_VALUE_SHORT:
        if (!xdr_short(&x, (short int *) &msg->value.v_short)) {
            return -1;
        }
        break;
    case GMETRIC_VALUE_UNSIGNED_INT:
        if (!xdr_u_int(&x, (u_int *) &msg->value.v_uint)) {
            return -1;
        }
        break;
    case GMETRIC_VALUE_INT:
        if (!xdr_int(&x, (int *) &msg->value.v_int)) {
            return -1;
        }
        break;
    case GMETRIC_VALUE_FLOAT:
        if (!xdr_float(&x, (float *) &msg->value.v_float)) {
            return -1;
        }
        break;
    case GMETRIC_VALUE_DOUBLE:
        if (!xdr_double(&x, (double *) &msg->value.v_double)) {
            return -1;
        }
        break;
    case GMETRIC_VALUE_STRING:
        if (!xdr_string(&x, (char**) &msg->value.v_string, ~0)) {
            return -1;
        }
        break;
    case GMETRIC_VALUE_UNKNOWN:
        if (!xdr_string(&x, (char**) &msg->value.v_string, ~0)) {
            return -1;
        }
        break;
    } */
    /* end switch */

    /* Force this to be a string, it's the standard ... */
    if (!xdr_string(&x, (char**) &msg->value.v_string, ~0)) {
        return -1;
    }

    return xdr_getpos(&x);
}

/**
 * "constructor"
 */
void gmetric_create(gmetric_t* g)
{
    /* zero out everything
     * and set socket to invalid
     */
    memset(g, 0, sizeof(gmetric_t));
    g->s = -1;
}

int gmetric_open_raw(gmetric_t* g, uint32_t ip, int port)
{
    if (g->s == -1) {
        gmetric_close(g);
    }
    g->s = -1;
    g->s = socket(AF_INET, SOCK_DGRAM,  IPPROTO_UDP);
    if (g->s == -1) {
        return 0;
    }

    memset(&g->sa, 0, sizeof(struct sockaddr_in));
    g->sa.sin_family = AF_INET;
    g->sa.sin_port = htons(port);
    memcpy(&(g->sa.sin_addr), &ip, sizeof(ip));
    return 1;
}

/*
 *  This is really a wrapper to gethostbyname
 */
int gmetric_open(gmetric_t* g, const char* addr, int port)
{
    struct hostent* result = NULL;
#ifdef __linux__
    /* super annoying thread safe version */
    /* linux 6-arg version                */
    struct hostent he;
    char tmpbuf[1024];
    int local_errno = 0;
    if (gethostbyname_r(addr, &he, tmpbuf, sizeof(tmpbuf),
                        &result, &local_errno)) {
        gmetric_close(g);
        return 0;
    }
#else
    /* Windows, HP-UX 11 and AIX 5 is thread safe */
    /* http://daniel.haxx.se/projects/portability/ */
    /* Mac OS X 10.2+ is thread safe */
    /* http://developer.apple.com/technotes/tn2002/tn2053.html */
    result = gethostbyname(addr);
#endif

    /* no result? no result2? not ip4? */
    if (result == NULL || result->h_addr_list[0] == NULL ||
        result->h_length != 4) {
        gmetric_close(g);
        return 0;
    }

    /* h_addr_list[0] is raw memory */
    uint32_t* ip = (uint32_t*) result->h_addr_list[0];
    return gmetric_open_raw(g, *ip, port);
}

int gmetric_send_xdr(gmetric_t* g, const char* buf, int len)
{
    return sendto(g->s, (char*)buf, len, 0,
                  (struct sockaddr*)&g->sa, sizeof(struct sockaddr_in));
}

int gmetric_send(gmetric_t* g, const gmetric_message_t* msg)
{
    char buf[GMETRIC_MAX_MESSAGE_LEN];
    int len = 0;
    int result1 = -1, result2 = -1;

    if (msg->format == GMETRIC_FORMAT_31) {
        len = gmetadata_message_create_xdr(buf, sizeof(buf), msg);
        result1 = gmetric_send_xdr(g, buf, len);

        if (result1 == -1) {
            return result1;
        }

        len = gmetric31_message_create_xdr(buf, sizeof(buf), msg);
        result2 = gmetric_send_xdr(g, buf, len);

        if (result2 == -1) {
           return result2;
        }

        return result1 = result2;
    } else {
        len = gmetric_message_create_xdr(buf, sizeof(buf), msg);
        return gmetric_send_xdr(g, buf, len);
    }
}

/**
 * "destructor"
 */
void gmetric_close(gmetric_t* g)
{
    if (g->s != -1) {
        close(g->s);
    }
    g->s = -1;
}

void gmetric_message_clear(gmetric_message_t* msg)
{
    msg->type = GMETRIC_VALUE_UNKNOWN;
    msg->format = GMETRIC_FORMAT_31;
    msg->hostname = "";
    msg->spoof = 0;
    msg->name = "";
    msg->group = "";
    msg->units = "";
    msg->typestr = "";
    msg->slope = GMETRIC_SLOPE_BOTH;
    msg->tmax = 60;
    msg->dmax = 0;
    msg->value.v_double = 0;
}

int gmetric_message_validate(const gmetric_message_t* msg)
{
    if (msg->format == GMETRIC_FORMAT_31 &&
        (msg->hostname == NULL || msg->hostname[0] == 0)) {
        return 0;
    }

    if (msg->type == GMETRIC_VALUE_UNKNOWN) {
        return 0;
    }

    if (msg->name == NULL || msg->name[0] == 0) {
        return 0;
    }

    if (msg->units == NULL || msg->units[0] == 0) {
        return 0;
    }

    if (msg->type == GMETRIC_VALUE_STRING &&
        (msg->value.v_string == NULL || msg->value.v_string[0] == 0)) {
        return 0;
    }

    return 1;
}

