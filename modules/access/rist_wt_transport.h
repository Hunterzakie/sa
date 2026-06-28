/*****************************************************************************
 * rist_wt_transport.h: RIST transport ops over a WebTransport datagram ring
 *****************************************************************************
 * Copyright (C) 2026 Sergio Ammirata
 *
 * Authors: Sergio Ammirata <sergio@ammirata.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/* rist_wt_transport_ops() returns a rist_transport_ops whose callbacks are
 * EM_JS thunks exchanging datagrams with the hosting page over a
 * SharedArrayBuffer ring pair. session_id selects which session's rings to use;
 * the host page allocates one session per RIST source with
 * rist_wt_create_session() (an EMSCRIPTEN_KEEPALIVE export consumed from JS).
 * Each session can be claimed by one source at a time: the call returns NULL if
 * that session is already in use or does not exist, and the caller must call
 * rist_wt_transport_ops_release() with the same id after stopping librist. */

#ifndef RIST_WT_TRANSPORT_H
#define RIST_WT_TRANSPORT_H

#ifdef __EMSCRIPTEN__

#include <librist/transport.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct rist_transport_ops *rist_wt_transport_ops(int session_id);
void rist_wt_transport_ops_release(int session_id);

#ifdef __cplusplus
}
#endif

#endif /* __EMSCRIPTEN__ */
#endif /* RIST_WT_TRANSPORT_H */
