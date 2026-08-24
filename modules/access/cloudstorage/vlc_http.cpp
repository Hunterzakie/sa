/*****************************************************************************
 * vlc_http.cpp: Native VLC HTTP client implementation for cloudstorage
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

#include "vlc_http.h"
#include <vlc_url.h>
#include <vlc_block.h>
#include <vlc_tls.h>
extern "C" {
#include "../http/connmgr.h"
#include "../http/message.h"
}

#include <sstream>
#include <string>
#include <map>
#include <iomanip>

namespace vlc {
namespace cloudstorage {

VLCHttpRequest::VLCHttpRequest(vlc_object_t* obj, struct vlc_http_mgr* mgr,
                               const std::string& url, const std::string& method,
                               bool follow_redirect)
    : m_obj(obj), m_mgr(mgr), m_url(url), m_method(method),
      m_follow_redirect(follow_redirect)
{}

VLCHttpRequest::~VLCHttpRequest() {}

void VLCHttpRequest::setParameter(const std::string& parameter, const std::string& value)
{
    m_get_params[parameter] = value;
}

void VLCHttpRequest::setHeaderParameter(const std::string& parameter, const std::string& value)
{
    m_headers[parameter] = value;
}

const VLCHttpRequest::GetParameters& VLCHttpRequest::parameters() const { return m_get_params; }
const VLCHttpRequest::HeaderParameters& VLCHttpRequest::headerParameters() const { return m_headers; }
const std::string& VLCHttpRequest::url() const { return m_url; }
const std::string& VLCHttpRequest::method() const { return m_method; }
bool VLCHttpRequest::follow_redirect() const { return m_follow_redirect; }


static void noop_frame_free(vlc_frame_t* frame) {
    free(frame);
}

static std::string url_encode(const std::string& s)
{
    std::ostringstream out;
    out << std::hex << std::uppercase;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out << (char)c;
        } else {
            out << '%' << std::setw(2) << std::setfill('0') << (int)c;
        }
    }
    return out.str();
}


void VLCHttpRequest::send(CompleteCallback on_completed,
                          std::shared_ptr<std::istream> data,
                          std::shared_ptr<std::ostream> response,
                          std::shared_ptr<std::ostream> error_stream,
                          ICallback::Pointer /*callback*/) const
{
    vlc_object_t*        obj        = m_obj;
    struct vlc_http_mgr* mgr        = m_mgr;
    std::string          url        = m_url;
    std::string          method     = m_method;
    GetParameters        get_params = m_get_params;
    HeaderParameters     headers    = m_headers;

    std::thread([=]() mutable {
        const bool is_body_method =
            (method == "POST" || method == "PUT" || method == "PATCH");

        std::string form_body;
        std::string full_url = url;

        if (is_body_method && !get_params.empty()) {
            bool first = true;
            for (const auto& kv : get_params) {
                if (!first) form_body += "&";
                form_body += url_encode(kv.first) + "=" + url_encode(kv.second);
                first = false;
            }
        } else if (!get_params.empty()) {
            full_url += (full_url.find('?') == std::string::npos) ? "?" : "&";
            bool first = true;
            for (const auto& kv : get_params) {
                if (!first) full_url += "&";
                full_url += kv.first + "=" + kv.second;
                first = false;
            }
        }

        vlc_url_t url_parsed;
        vlc_UrlParse(&url_parsed, full_url.c_str());

        bool     https = (url_parsed.psz_protocol &&
                          std::string(url_parsed.psz_protocol) == "https");
        unsigned port  = url_parsed.i_port;
        if (port == 0) port = https ? 443 : 80;

        const char* path = url_parsed.psz_path ? url_parsed.psz_path : "/";
        std::string full_path = path;
        if (url_parsed.psz_option) {
            full_path += "?";
            full_path += url_parsed.psz_option;
        }

        std::string body_str;
        bool has_payload = false;

        if (!form_body.empty()) {
            body_str = form_body;
            has_payload = true;
            headers["Content-Type"] = "application/x-www-form-urlencoded";
        } else if (data && is_body_method) {
            std::ostringstream oss;
            oss << data->rdbuf();
            body_str = oss.str();
            has_payload = is_body_method;

            if (!body_str.empty() && body_str[0] != '{' && body_str[0] != '[') {
                headers["Content-Type"] = "application/x-www-form-urlencoded";
            } else if (!body_str.empty()) {
                headers["Content-Type"] = "application/json";
            }
        }

        struct vlc_http_msg* req = vlc_http_req_create(
                method.c_str(), https ? "https" : "http",
                url_parsed.psz_host, full_path.c_str());
        if (!req) {
            msg_Err(obj, "VLCHttp: Failed to create request object");
            vlc_UrlClean(&url_parsed);
            if (error_stream) *error_stream << "Failed to create HTTP request";
            on_completed({IHttpRequest::Failure, {}, response, error_stream});
            return;
        }

        if (has_payload) {
            vlc_http_msg_add_header(req, "Content-Length", "%zu", body_str.size());
        }

        for (const auto& kv : headers)
            vlc_http_msg_add_header(req, kv.first.c_str(), "%s", kv.second.c_str());

        block_t *payload = nullptr;
        if (has_payload) {
            static const struct vlc_frame_callbacks frameCbs = {
                &noop_frame_free
            };
            payload = vlc_frame_New(&frameCbs, (void*)body_str.data(), body_str.size());
        }

        struct vlc_http_msg* resp = vlc_http_mgr_request(
            mgr, https, url_parsed.psz_host, port, req, false, payload, true);
        vlc_UrlClean(&url_parsed);
        vlc_http_msg_destroy(req);

        if (!resp) {
            msg_Err(obj, "VLCHttp: Connection failed (null response from mgr)");
            if (error_stream) *error_stream << "Connection failed";
            on_completed({IHttpRequest::Failure, {}, response, error_stream});
            return;
        }

        int status = vlc_http_msg_get_status(resp);
        HeaderParameters resp_headers;

        static const char *const interesting_headers[] = {
            "content-range",
            "content-length",
            "content-type",
            "location",
            "accept-ranges",
        };
        for (const char *h : interesting_headers) {
            const char *val = vlc_http_msg_get_header(resp, h);
            if (val)
                resp_headers[h] = val;
        }

        auto out = IHttpRequest::isSuccess(status) ? response : error_stream;
        while (true) {
            block_t* b = vlc_http_msg_read(resp);
            if (!b) break;
            if (out && b->i_buffer > 0) {
                out->write(reinterpret_cast<const char*>(b->p_buffer), b->i_buffer);
            }
            block_Release(b);
        }

        vlc_http_msg_destroy(resp);
        on_completed({status, resp_headers, response, error_stream});
    }).detach();
}

VLCHttp::VLCHttp(vlc_object_t* obj) : m_obj(obj)
{
    m_mgr = vlc_http_mgr_create(obj, nullptr);
}

VLCHttp::~VLCHttp()
{
    if (m_mgr)
        vlc_http_mgr_destroy(m_mgr);
}

::cloudstorage::IHttpRequest::Pointer VLCHttp::create(
        const std::string& url, const std::string& method,
        bool follow_redirect) const
{
    std::string real_method = method.empty() ? "GET" : method;
    return std::make_unique<VLCHttpRequest>(m_obj, m_mgr, url, real_method, follow_redirect);
}

}
}
