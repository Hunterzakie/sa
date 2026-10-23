/*****************************************************************************
 * vlc_http.h: Native VLC HTTP client header for cloudstorage
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

#ifndef VLC_CLOUDSTORAGE_HTTP_H
#define VLC_CLOUDSTORAGE_HTTP_H

#include <vlc_common.h>
#include <cloudstorage/IHttp.h>
#include <thread>
#include <mutex>
#include <string>

struct vlc_http_mgr;

namespace vlc {
namespace cloudstorage {

class VLCHttpRequest : public ::cloudstorage::IHttpRequest {
public:
    VLCHttpRequest(vlc_object_t* obj, struct vlc_http_mgr* mgr, const std::string& url, const std::string& method, bool follow_redirect);
    ~VLCHttpRequest() override;

    void setParameter(const std::string& parameter, const std::string& value) override;
    void setHeaderParameter(const std::string& parameter, const std::string& value) override;

    const GetParameters& parameters() const override;
    const HeaderParameters& headerParameters() const override;
    const std::string& url() const override;
    const std::string& method() const override;
    bool follow_redirect() const override;

    void send(CompleteCallback on_completed,
              std::shared_ptr<std::istream> data,
              std::shared_ptr<std::ostream> response,
              std::shared_ptr<std::ostream> error_stream = nullptr,
              ICallback::Pointer callback = nullptr) const override;

private:
    vlc_object_t* m_obj;
    struct vlc_http_mgr* m_mgr;
    std::string m_url;
    std::string m_method;
    bool m_follow_redirect;
    GetParameters m_get_params;
    HeaderParameters m_headers;
};

class VLCHttp : public ::cloudstorage::IHttp {
public:
    explicit VLCHttp(vlc_object_t* obj);
    ~VLCHttp() override;

    ::cloudstorage::IHttpRequest::Pointer create(const std::string& url, const std::string& method = "GET", bool follow_redirect = true) const override;

private:
    vlc_object_t* m_obj;
    struct vlc_http_mgr* m_mgr;
};

} // namespace cloudstorage
} // namespace vlc

#endif // VLC_CLOUDSTORAGE_HTTP_H
