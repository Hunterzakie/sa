/*****************************************************************************
 * command.h
 *****************************************************************************
 * Copyright © 2022 VLC authors and VideoLAN
 *
 * Author: Lucas Ransan <lucas@ransan.tk>
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

#ifndef VLC_MPD_COMMAND_H
#define VLC_MPD_COMMAND_H

#include "mpd.h"

typedef enum mpd_cmd_result_t {
    /* The function has sent the error with ACK */
    MPD_CMD_ERROR = -1,

    /* The function succeeded, still need to send OK/list_OK */
    MPD_CMD_OK,

    /* The function succeeded, no need to send OK/list_OK */
    MPD_CMD_SILENT_OK,
} mpd_cmd_result_t;

/* argv: NULL terminated array of the command and its arguments */
typedef mpd_cmd_result_t (*mpd_command_t)(intf_thread_t *intf, mpd_client_t *client, char **argv);

int mpd_command_find_and_call(intf_thread_t *intf, mpd_client_t *client, char **argv);
void mpd_assert_commands_sorted(intf_thread_t *intf);

#endif
