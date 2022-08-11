/*****************************************************************************
 * mpd.c
 *****************************************************************************
 * Copyright © 2021-2022 VLC authors and VideoLAN
 *
 * Authors: George Vaindirlis <george.vdl1@gmail.com>
 *          Lucas Ransan <lucas@ransan.tk>
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_interface.h>
#include <vlc_network.h>
#include <vlc_fs.h>
#include <vlc_url.h>
#include <vlc_memstream.h>

#include "mpd.h"
#include "protocol.h"
#include "command.h"
#include "playlist.h"
#include "medialibrary.h"

#define DEFAULT_PORT 6600

#define MPD_OK 0
#define MPD_CLIENT_REMOVE 1

char *mpd_mrl2path(intf_thread_t *intf, const char *mrl) {
    intf_sys_t *sys = intf->p_sys;

    size_t prefix_len;
    const char *ep_name = NULL;

    vlc_mutex_lock(&sys->ep_lock);
    for (size_t i = 0; i < sys->entry_points.size; i++) {
        mpd_entry_point_t *ep = &sys->entry_points.data[i];
        prefix_len = strlen(ep->mrl);

        /* TODO: entry points with same prefix */
        if (strncmp(mrl, ep->mrl, prefix_len) == 0) {
            ep_name = ep->name;
            break;
        }
    }
    if (!ep_name) {
        vlc_mutex_unlock(&sys->ep_lock);
        return NULL;
    }
    size_t ep_len = strlen(ep_name);

    mrl += prefix_len;
    size_t mrl_len = strlen(mrl);

    char *res = malloc(ep_len + mrl_len + 1);
    if (!res) {
        vlc_mutex_unlock(&sys->ep_lock);
        return NULL;
    }

    memcpy(res, ep_name, ep_len);
    vlc_mutex_unlock(&sys->ep_lock);
    memcpy(res + ep_len, mrl, mrl_len);

    /* Remove trailing slash */
    if (mrl[mrl_len - 1] == '/')
        res[ep_len + mrl_len - 1] = '\0';
    else
        res[ep_len + mrl_len] = '\0';

    if (!vlc_uri_decode(res + ep_len)) {
        free(res);
        return NULL;
    }

    return res;
}

char *mpd_path2mrl(intf_thread_t *intf, const char *path) {
    intf_sys_t *sys = intf->p_sys;

    const char *slash = strchrnul(path, '/');
    size_t prefix_len = slash - path;

    vlc_mutex_lock(&sys->ep_lock);
    const char *ep_mrl = NULL;
    for (size_t i = 0; i < sys->entry_points.size; i++) {
        mpd_entry_point_t *ep = &sys->entry_points.data[i];

        if (strncmp(path, ep->name, prefix_len) == 0 && strlen(ep->name) == prefix_len) {
            ep_mrl = ep->mrl;
            break;
        }
    }
    if (!ep_mrl) {
        vlc_mutex_unlock(&sys->ep_lock);
        return NULL;
    }

    struct vlc_memstream stream;
    vlc_memstream_open(&stream);
    vlc_memstream_puts(&stream, ep_mrl);
    vlc_mutex_unlock(&sys->ep_lock);

    for (path = slash; *path != '\0'; path = slash) {
        path++;
        slash = strchrnul(path, '/');

        char *s = strndup(path, slash - path);
        if (!s)
            goto error;

        char *es = vlc_uri_encode(s);
        free(s);
        if (!es)
            goto error;

        vlc_memstream_printf(&stream, "/%s", es);
        free(es);
    }

    if (vlc_memstream_close(&stream))
        return NULL;
    return stream.ptr;

error:
    if (!vlc_memstream_close(&stream))
        free(stream.ptr);
    return NULL;
}

void write_to_client_buffer(intf_thread_t *intf, mpd_client_t *client, const char *buf) {
    intf_sys_t *sys = intf->p_sys;

    msg_Dbg(intf, "sending: %.*s", (int)(strlen(buf) - 1), buf);
    if (vlc_vector_push_all(&client->sendbuf, buf, strlen(buf)))
        sys->fds.data[client->pfd].events |= POLLOUT;

    /* Wakeup from poll */
    int rc = write(sys->pipe[1], (char[]){MPD_WAKE_UP}, 1);
    if (rc <= 0)
        msg_Err(intf, "Could not wake up the main loop: %s",
                vlc_strerror_c(errno));
}

void mpd_idle_events_update(intf_thread_t *intf, int event) {
    intf_sys_t *sys = intf->p_sys;

    mpd_client_t *client;
    vlc_list_foreach(client, &sys->clients, node) {
        client->idle_events |= event;
        if (client->idle && client->idle_events_waiting_for & event) {
            client->idle = false;
            mpd_send_changed_subsystems(intf, client);
            write_to_client_buffer(intf, client, "OK\n");
        }
    }
}

void mpd_send_idle_events_update(intf_thread_t *intf, int event) {
    intf_sys_t *sys = intf->p_sys;

    uint8_t data[3] = {MPD_CHANGED_SUBSYSTEMS};
    *(uint16_t *)&data[1] = event;
    ssize_t written = write(sys->pipe[1], data, sizeof data);
    VLC_UNUSED(written);
}

static int handle_client_input(intf_thread_t *intf, mpd_client_t *client) {
    char *newline;
    while ((newline = memchr(client->recvbuf.data, '\n', client->recvbuf.size))) {
        char *command_list_end = NULL;
        if (client->in_command_list) {
            const char *needle = "command_list_end\n";
            char *end = memmem(client->recvbuf.data, client->recvbuf.size, needle, strlen(needle));
            if (!end || (end != client->recvbuf.data && end[-1] != '\n'))
                /* Can't find the end of the command list, we need more data */
                break;
            command_list_end = end;
        }

        ptrdiff_t nl_index = newline - client->recvbuf.data;
        assert(0 <= nl_index && (size_t)nl_index < client->recvbuf.size);

        char *req = strndup(client->recvbuf.data, nl_index);
        if (!req)
            return VLC_ENOMEM;
        vlc_vector_remove_slice(&client->recvbuf, 0, nl_index + 1);

        msg_Dbg(intf, "request: %s", req);

        char *tokens[MPD_MAXTOK + 1];
        mpd_tokenize_request(intf, req, tokens);

        mpd_cmd_result_t rc = mpd_command_find_and_call(intf, client, tokens);
        free(req);
        switch (rc) {
        case MPD_CMD_ERROR:
            if (client->in_command_list) {
                vlc_vector_remove_slice(&client->recvbuf, 0,
                                        command_list_end - client->recvbuf.data + strlen("command_list_end\n"));
                client->in_command_list = MPD_CL_NO;
                client->command_number_in_list = 0;
            }
            continue;

        case MPD_CMD_OK:
            switch (client->in_command_list) {
            case MPD_CL_NO:
                write_to_client_buffer(intf, client, "OK\n");
                break;
            case MPD_CL_YES:
                client->command_number_in_list++;
                break;
            case MPD_CL_OK_YES:
                /* TODO: handle idle? */
                write_to_client_buffer(intf, client, "list_OK\n");
                client->command_number_in_list++;
                break;
            }
            break;

        case MPD_CMD_SILENT_OK:
            break;
        }
    }

    return 0;
}

static mpd_client_t *find_client(intf_thread_t *intf, size_t pfd) {
    intf_sys_t *sys = intf->p_sys;

    mpd_client_t *client;
    vlc_list_foreach(client, &sys->clients, node)
        if (client->pfd == pfd)
            return client;

    return NULL;
}

static int handle_client(intf_thread_t *intf, mpd_client_t *client) {
    intf_sys_t *sys = intf->p_sys;
    struct pollfd *pfd = &sys->fds.data[client->pfd];

    /* TODO: add POLLHUP to pfd->events */
    if (pfd->revents & POLLHUP) {
        msg_Dbg(intf, "received POLLHUP");
        return MPD_CLIENT_REMOVE;
    }

    if (pfd->revents & POLLIN) {
        char recvbuf[1024];
        ssize_t r = read(pfd->fd, recvbuf, 1024);
        if (r < 0) {
            msg_Err(intf, "read failed: %s", vlc_strerror_c(errno));
            return MPD_CLIENT_REMOVE;
        }

        if (!vlc_vector_push_all(&client->recvbuf, recvbuf, r))
            return VLC_ENOMEM;
        int rc = handle_client_input(intf, client);
        if (rc < 0)
            return rc;
    }

    if (pfd->revents & POLLOUT) {
        ssize_t written = vlc_write(pfd->fd, client->sendbuf.data, client->sendbuf.size);
        if (written < 0) {
            msg_Err(intf, "vlc_write failed: %s", vlc_strerror_c(errno));
            return MPD_CLIENT_REMOVE;
        }

        if ((size_t)written == client->sendbuf.size) {
            vlc_vector_clear(&client->sendbuf);
            pfd->events = POLLIN;
        } else {
            vlc_vector_remove_slice(&client->sendbuf, 0, written);
        }
    }

    return MPD_OK;
}

static mpd_client_t *mpd_client_new(size_t pfd) {
    mpd_client_t *client = malloc(sizeof *client);
    if (client == NULL)
        return NULL;

    vlc_vector_init(&client->recvbuf);
    vlc_vector_init(&client->sendbuf);
    client->pfd = pfd;
    client->in_command_list = MPD_CL_NO;
    client->current_command = NULL;
    client->command_number_in_list = 0;
    client->idle_events = 0;
    client->idle_events_waiting_for = 0;
    client->idle = false;

    return client;
}

static void mpd_client_destroy(mpd_client_t *client) {
    vlc_list_remove(&client->node);
    vlc_vector_destroy(&client->recvbuf);
    vlc_vector_destroy(&client->sendbuf);
    free(client);
}

static void decrease_pfd_indexes(intf_sys_t *sys, size_t removed_pfd_index) {
    mpd_client_t *client;
    vlc_list_foreach(client, &sys->clients, node)
        if (client->pfd > removed_pfd_index)
            client->pfd--;
}

static void handle_new_clients(intf_thread_t *intf, int sock) {
    intf_sys_t *sys = intf->p_sys;

    for (;;) {
        union {
            struct sockaddr a;
            struct sockaddr_storage s;
        } addr;
        int cfd = vlc_accept(sock, &addr.a, &(socklen_t){sizeof addr}, true);
        if (cfd < 0) {
            if (net_errno == EWOULDBLOCK
#if (EAGAIN != EWOULDBLOCK)
                || net_errno == EAGAIN
#endif
               ) {
                /* We will try later */
                return;
            } else {
                msg_Err(intf, "accept failed: %s", vlc_strerror_c(net_errno));
                goto accept_error;
            }
        }

        char host[NI_MAXNUMERICHOST];
        int port;
        int rc = vlc_getnameinfo(&addr.a, sizeof addr, host, sizeof host, &port, 0);
        if (rc == 0) {
            msg_Info(intf, "MPD Server: connecting %s:%d", host, port);
        } else {
            msg_Warn(intf, "vlc_getnameinfo failed: %s", gai_strerror(rc));
            msg_Info(intf, "MPD Server: connecting");
        }

        struct pollfd pfd = {
            .fd = cfd,
        };
        if (!vlc_vector_push(&sys->fds, pfd))
            goto pfd_push_error;

        mpd_client_t *client = mpd_client_new(sys->fds.size - 1);
        if (!client)
            goto client_new_error;
        vlc_list_append(&client->node, &sys->clients);

        mpd_greet_client(intf, client);
        return;

client_new_error:
        vlc_vector_remove(&sys->fds, sys->fds.size - 1);
pfd_push_error:
        msg_Err(intf, "cannot connect client: Out of memory");
accept_error:
        ;
    }
}

static bool is_fd_in(int fd, int *fds) {
    for (int i = 0; fds[i] != -1; i++)
        if (fds[i] == fd)
            return true;
    return false;
}

static void *Run(void *data) {
    intf_thread_t *intf = data;
    intf_sys_t *sys = intf->p_sys;

    vlc_thread_set_name("vlc-mpd");

    int err = mpd_ml_gen_entry_points(intf);
    if (err < 0) {
        msg_Err(intf, "Failed to generate entry points names");
        return NULL;
    }

    for (int i = 0; sys->sockv[i] != -1; i++) {
        struct pollfd server_pfd = {
            .fd = sys->sockv[i],
            .events = POLLIN,
        };
        if (!vlc_vector_push(&sys->fds, server_pfd))
            return NULL;
    }
    {
        struct pollfd pipe_pfd = {
            .fd = sys->pipe[0], /* read end of the pipe */
            .events = POLLIN,
        };
        if (!vlc_vector_push(&sys->fds, pipe_pfd))
            return NULL;
    }

    bool stop_server = false;
    while (!stop_server) {
        int rc = poll(sys->fds.data, sys->fds.size, -1);
        if (rc < 0) {
            msg_Err(intf, "poll failed: %s", vlc_strerror_c(errno));
            return NULL;
        }

        size_t i = 0;
        while (i < sys->fds.size) {
            struct pollfd *pfd = &sys->fds.data[i];
            bool remove_client = false;

            if (pfd->revents == 0) {
            } else if (pfd->revents & POLLERR) {
                int error = 0;
                if (getsockopt(pfd->fd, SOL_SOCKET, SO_ERROR, &error, &(socklen_t){sizeof error}) == 0)
                    msg_Err(intf, "error on fd %d: %s", pfd->fd, vlc_strerror_c(error));
                else
                    msg_Err(intf, "getsockopt: %s", vlc_strerror_c(errno));
                remove_client = true;
            } else if (pfd->fd == sys->pipe[0]) {
                char action;
                ssize_t r = read(pfd->fd, &action, 1);
                if (r == 1) {
                    switch (action) {
                    case MPD_WAKE_UP:
                        break;
                    case MPD_STOP_SERVER:
                        stop_server = true;
                        break;
                    case MPD_CHANGED_SUBSYSTEMS: {
                        uint16_t subsystems;
                        if (read(sys->pipe[0], &subsystems, 2) != 2) {
                            msg_Err(intf, "read pipe: %s", vlc_strerror_c(errno));
                            break;
                        }
                        mpd_idle_events_update(intf, subsystems);
                        break;
                    }
                    case MPD_GEN_ML_ENTRY_POINTS:
                        mpd_ml_gen_entry_points(intf);
                        break;
                    }
                } else {
                    msg_Err(intf, "read pipe: %s", vlc_strerror_c(errno));
                    stop_server = true;
                }
            } else if (is_fd_in(pfd->fd, sys->sockv)) {
                handle_new_clients(intf, pfd->fd);
            } else {
                mpd_client_t *client = find_client(intf, i);
                if (client == NULL) {
                    msg_Err(intf, "could not find client with fd %d", pfd->fd);
                    remove_client = true;
                } else {
                    rc = handle_client(intf, client);
                    if (rc == MPD_CLIENT_REMOVE)
                        remove_client = true;
                }
            }

            if (remove_client) {
                msg_Info(intf, "removing client");
                vlc_vector_remove(&sys->fds, i);

                mpd_client_t *client = find_client(intf, i);
                if (client)
                    mpd_client_destroy(client);
                decrease_pfd_indexes(sys, i);
            } else {
                i++;
            }
        }
    }

    return NULL;
}

static int Open(vlc_object_t *obj) {
    intf_thread_t *intf = (intf_thread_t *)obj;

    mpd_assert_commands_sorted(intf);

    intf_sys_t *sys = malloc(sizeof *sys);
    if (sys == NULL)
        return VLC_ENOMEM;
    intf->p_sys = sys;

    vlc_mutex_init(&sys->pl_lock);
    vlc_mutex_init(&sys->ep_lock);
    vlc_list_init(&sys->clients);
    vlc_vector_init(&sys->fds);
    vlc_vector_init(&sys->pl);
    vlc_vector_init(&sys->entry_points);

    sys->playlist_version = 0;

    int rc = vlc_pipe(sys->pipe);
    if (rc < 0) {
        msg_Err(intf, "vlc_pipe failed: %s", vlc_strerror_c(errno));
        goto error_pipe;
    }

    char *host = var_InheritString(intf, "mpd-host");
    int port = var_InheritInteger(intf, "mpd-port");

    int *sockv = net_ListenTCP(intf, host, port);
    free(host);
    if (!sockv)
        goto error_listen;
    sys->sockv = sockv;

    sys->playlist = vlc_intf_GetMainPlaylist(intf);
    if (!sys->playlist) {
        msg_Err(intf, "Could not get playlist");
        goto error_playlist;
    }

    sys->player = vlc_playlist_GetPlayer(sys->playlist);
    if (!sys->player) {
        msg_Err(intf, "Could not get player");
        goto error_player;
    }

    sys->medialibrary = vlc_ml_instance_get(obj);
    if (!sys->medialibrary) {
        msg_Err(intf, "Could not get media library");
        goto error_ml;
    }

    int err = mpd_playlist_callbacks_setup(intf);
    if (err)
        goto error_playlist_callbacks;

    err = mpd_ml_callbacks_setup(intf);
    if (err)
        goto error_ml_callbacks;

    msg_Info(intf, "Starting MPD server (port: %d)", port);

    if (vlc_clone(&sys->thread, Run, intf))
        goto error_clone;

    return VLC_SUCCESS;

error_clone:
    vlc_ml_event_unregister_callback(sys->medialibrary, sys->ml_event_callback);
error_ml_callbacks:
    vlc_playlist_Lock(sys->playlist);
    vlc_playlist_RemoveListener(sys->playlist, sys->playlist_listener);
    vlc_player_aout_RemoveListener(sys->player, sys->player_aout_listener);
    vlc_player_RemoveListener(sys->player, sys->player_listener);
    vlc_playlist_Unlock(sys->playlist);
error_playlist_callbacks:
    ;
error_ml:
    ;
error_player:
    ;
error_playlist:
    for (int i = 0; sockv[i] != -1; i++)
        net_Close(sockv[i]);
    free(sockv);
error_listen:
    close(sys->pipe[0]);
    close(sys->pipe[1]);
error_pipe:
    vlc_vector_destroy(&sys->fds);
    vlc_vector_destroy(&sys->pl);
    vlc_vector_destroy(&sys->entry_points);
    free(sys);
    return VLC_EGENERIC;
}

static void Close(vlc_object_t *obj) {
    intf_thread_t *intf = (intf_thread_t *)obj;
    intf_sys_t *sys = intf->p_sys;

    ssize_t rc;
    do
        rc = write(sys->pipe[1], (char[]){MPD_STOP_SERVER}, 1);
    while (rc < 0 && errno == EINTR);
    if (rc <= 0)
        msg_Err(intf, "Could not wake up the main loop: %s",
                vlc_strerror_c(errno));

    /* The poll thread still references sys; join it before tearing anything
     * down, even if the wake-up write failed. */
    vlc_join(sys->thread, NULL);

    vlc_playlist_Lock(sys->playlist);
    vlc_playlist_RemoveListener(sys->playlist, sys->playlist_listener);
    vlc_player_aout_RemoveListener(sys->player, sys->player_aout_listener);
    vlc_player_RemoveListener(sys->player, sys->player_listener);
    vlc_playlist_Unlock(sys->playlist);
    vlc_ml_event_unregister_callback(sys->medialibrary, sys->ml_event_callback);

    mpd_client_t *client;
    vlc_list_foreach(client, &sys->clients, node)
        mpd_client_destroy(client);

    struct pollfd pfd;
    vlc_vector_foreach(pfd, &sys->fds)
        close(pfd.fd);
    vlc_vector_destroy(&sys->fds);

    free(sys->sockv);
    close(sys->pipe[0]);
    close(sys->pipe[1]);

    vlc_vector_destroy(&sys->pl);

    for (size_t i = 0; i < sys->entry_points.size; i++) {
        free(sys->entry_points.data[i].mrl);
        free(sys->entry_points.data[i].name);
    }
    vlc_vector_destroy(&sys->entry_points);

    free(sys);
}

vlc_module_begin()
    set_shortname(N_("MPD Server"))
    set_description(N_("MPD Server interface"))
    set_capability("interface", 0)
    set_callbacks(Open, Close)
    set_subcategory(SUBCAT_INTERFACE_CONTROL)
    add_string("mpd-host", NULL, N_("MPD server address"), N_(""));
    add_integer("mpd-port", DEFAULT_PORT, N_("MPD Port"), N_("Port to use for the MPD server"));
vlc_module_end()
