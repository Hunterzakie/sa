/*****************************************************************************
 * vlc_httpd_server.cpp: Embedded OAuth redirect server for cloudstorage
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "vlc_httpd_server.h"
#include <vlc_httpd.h>
#include <vlc_url.h>
#include <vlc_variables.h>
#include <cstring>
#include <vector>

namespace vlc {
namespace cloudstorage {

class VLCHttpServerRequest : public ::cloudstorage::IHttpServer::IRequest {
public:
    VLCHttpServerRequest(const httpd_message_t* query) : m_query(query) {
        if (query->psz_args) {
            std::string args(reinterpret_cast<const char*>(query->psz_args));
            size_t pos = 0;
            while (pos < args.length()) {
                size_t next = args.find('&', pos);
                std::string pair = (next == std::string::npos) ? args.substr(pos) : args.substr(pos, next - pos);
                size_t eq = pair.find('=');
                if (eq != std::string::npos) {
                    std::string key = pair.substr(0, eq);
                    std::string val = pair.substr(eq + 1);
                    vlc_uri_decode(&val[0]);
                    m_args[key] = val.c_str();
                }
                if (next == std::string::npos) break;
                pos = next + 1;
            }
        }
    }

    const char* get(const std::string& name) const override {
        auto it = m_args.find(name);
        if (it != m_args.end()) return it->second.c_str();
        return nullptr;
    }

    const char* header(const std::string& name) const override {
        const char* val = httpd_MsgGet(m_query, name.c_str());
        return (val && val[0]) ? val : nullptr;
    }

    std::string method() const override {
        switch (m_query->i_type) {
            case HTTPD_MSG_GET: return "GET";
            case HTTPD_MSG_POST: return "POST";
            case HTTPD_MSG_HEAD: return "HEAD";
            default: return "GET";
        }
    }

    std::string url() const override {
        return m_query->psz_url ? m_query->psz_url : "/";
    }

    ::cloudstorage::IHttpServer::IResponse::Pointer response(
        int code, const ::cloudstorage::IHttpServer::IResponse::Headers& headers, int size,
        ::cloudstorage::IHttpServer::IResponse::ICallback::Pointer cb) const override;

private:
    const httpd_message_t* m_query;
    std::unordered_map<std::string, std::string> m_args;
};

class VLCHttpServerResponse : public ::cloudstorage::IHttpServer::IResponse {
public:
    VLCHttpServerResponse(int code, const Headers& headers, int size, ICallback::Pointer cb)
        : m_code(code), m_headers(headers), m_size(size), m_cb(std::move(cb)) {}

    void resume() override {}
    void completed(CompletedCallback cb) override {
        if (cb) cb();
    }

    int m_code;
    Headers m_headers;
    int m_size;
    ICallback::Pointer m_cb;
};

::cloudstorage::IHttpServer::IResponse::Pointer VLCHttpServerRequest::response(
    int code, const ::cloudstorage::IHttpServer::IResponse::Headers& headers, int size,
    ::cloudstorage::IHttpServer::IResponse::ICallback::Pointer cb) const {
    return std::make_unique<VLCHttpServerResponse>(code, headers, size, std::move(cb));
}

VLCHttpServer::VLCHttpServer(vlc_object_t* obj, ICallback::Pointer callback, Type type)
    : m_obj(obj), m_callback(std::move(callback)), m_type(type), m_host(nullptr), m_url(nullptr)
{
    int port = (type == Type::Authorization) ? 12345 : 12346;
    var_Create(obj, "http-port", VLC_VAR_INTEGER);
    var_SetInteger(obj, "http-port", port);

    m_host = vlc_http_HostNew(obj);
    if (!m_host) {
        msg_Err(obj, "Failed to create HTTP host on port %d (port already in use?)", port);
        return;
    }
    m_url = httpd_UrlNew(m_host, "/", nullptr, nullptr);
    if (!m_url) {
        msg_Err(obj, "Failed to register HTTP URL handler");
        return;
    }
    httpd_UrlCatch(m_url, HTTPD_MSG_GET, httpd_callback, reinterpret_cast<httpd_callback_sys_t*>(this));
}

VLCHttpServer::~VLCHttpServer()
{
    if (m_url) httpd_UrlDelete(m_url);
    if (m_host) httpd_HostDelete(m_host);
}

::cloudstorage::IHttpServer::ICallback::Pointer VLCHttpServer::callback() const
{
    return m_callback;
}

bool VLCHttpServer::ok() const
{
    return m_host != nullptr && m_url != nullptr;
}

int VLCHttpServer::httpd_callback(httpd_callback_sys_t* sys, httpd_client_t* client, httpd_message_t* answer, const httpd_message_t* query)
{
    (void)client;
    VLCHttpServer* self = static_cast<VLCHttpServer*>(reinterpret_cast<void*>(sys));

    VLCHttpServerRequest req(query);
    auto response_ptr = self->m_callback->handle(req);
    if (!response_ptr) return VLC_EGENERIC;

    auto resp = static_cast<VLCHttpServerResponse*>(response_ptr.get());

    answer->i_proto = HTTPD_PROTO_HTTP;
    answer->i_version = 0; // HTTP/1.0
    answer->i_type = HTTPD_MSG_ANSWER;
    answer->i_status = resp->m_code;

    for (const auto& kv : resp->m_headers) {
        httpd_MsgAdd(answer, kv.first.c_str(), "%s", kv.second.c_str());
    }

    if (httpd_MsgGet(query, "Connection") != nullptr) {
        httpd_MsgAdd(answer, "Connection", "close");
    }

    if (resp->m_cb) {
        std::vector<char> body_data;
        char buffer[8192];
        while (true) {
            int ret = resp->m_cb->putData(buffer, sizeof(buffer));
            if (ret > 0) {
                body_data.insert(body_data.end(), buffer, buffer + ret);
            } else if (ret == ::cloudstorage::IHttpServer::IResponse::ICallback::End) {
                break;
            } else if (ret == ::cloudstorage::IHttpServer::IResponse::ICallback::Abort) {
                msg_Warn(self->m_obj, "VLCHttpServer: Callback aborted body retrieval");
                break; 
            } else if (ret == ::cloudstorage::IHttpServer::IResponse::ICallback::Suspend) {
                msg_Warn(self->m_obj, "VLCHttpServer: Callback suspended body retrieval");
                break; 
            }
        }

        if (!body_data.empty()) {
            answer->p_body = static_cast<uint8_t*>(malloc(body_data.size()));
            if (answer->p_body) {
                memcpy(answer->p_body, body_data.data(), body_data.size());
                answer->i_body = body_data.size();
            }
        }
    }

    httpd_MsgAdd(answer, "Content-Length", "%zu", answer->i_body);

    resp->completed(nullptr);
    return VLC_SUCCESS;
}

VLCHttpServerFactory::VLCHttpServerFactory(vlc_object_t* obj) : m_obj(obj) {}

::cloudstorage::IHttpServer::Pointer VLCHttpServerFactory::create(
    ::cloudstorage::IHttpServer::ICallback::Pointer callback,
    const std::string& session_id,
    ::cloudstorage::IHttpServer::Type type)
{
    (void)session_id;
    auto server = std::make_unique<VLCHttpServer>(m_obj, std::move(callback), type);
    if (!server->ok())
        return nullptr;
    return server;
}

} 
}
