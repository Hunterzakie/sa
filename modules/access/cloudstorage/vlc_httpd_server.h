/*****************************************************************************
 * vlc_httpd_server.h: Embedded OAuth redirect server for cloudstorage
 *****************************************************************************
 * Copyright (C) 2026 VideoLabs and VideoLAN
 *
 * Authors: Ashish Vardhan Karanam <ashishkaranam06@gmail.com>
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

#ifndef VLC_CLOUDSTORAGE_HTTPD_SERVER_H
#define VLC_CLOUDSTORAGE_HTTPD_SERVER_H

#include <vlc_common.h>
#include <cloudstorage/IHttpServer.h>
#include <memory>
#include <string>

#include <vlc_httpd.h>

namespace vlc {
namespace cloudstorage {

class VLCHttpServer : public ::cloudstorage::IHttpServer {
public:
    VLCHttpServer(vlc_object_t* obj, ICallback::Pointer callback, Type type);
    ~VLCHttpServer() override;

    ICallback::Pointer callback() const override;
    bool ok() const;

private:
    static int httpd_callback(httpd_callback_sys_t* sys, httpd_client_t* client, httpd_message_t* answer, const httpd_message_t* query);

    vlc_object_t* m_obj;
    ICallback::Pointer m_callback;
    Type m_type;
    httpd_host_t* m_host;
    httpd_url_t* m_url;
};

class VLCHttpServerFactory : public ::cloudstorage::IHttpServerFactory {
public:
    explicit VLCHttpServerFactory(vlc_object_t* obj);
    ~VLCHttpServerFactory() override = default;

    ::cloudstorage::IHttpServer::Pointer create(::cloudstorage::IHttpServer::ICallback::Pointer callback,
                                                const std::string& session_id,
                                                ::cloudstorage::IHttpServer::Type type) override;

private:
    vlc_object_t* m_obj;
};

}
}

#endif 
