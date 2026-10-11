/*****************************************************************************
 * google_api_handler.cpp: Implementation for Google API connection
 *****************************************************************************
 * Copyright (C) 2025 VideoLabs and VideoLAN
 *
 * Authors: Maksym Yemelianenko <max.yemelianenko@gmail.com>
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
#include "google_api_handler.h"

#include <vlc_common.h>
#include <vlc_stream.h>
#include <vlc_url.h>
#include <vlc_variables.h>

extern "C" {
#include "../http/connmgr.h"
#include "../http/message.h"
}

#include <string>
#include <cstring>

static int StartRequest(stream_t *p_access, uint64_t range_from);
static int ParseResponse(stream_t *p_access);
static block_t *GoogleApiBlock(stream_t *p_access, bool *eof);
static int GoogleApiSeek(stream_t *p_access, uint64_t i_pos);
static int GoogleApiControl(stream_t *p_access, int i_query, va_list args);
static bool ends_with_ci(const char *host, const char *suffix);
static bool IsGoogleDomain(const char *host);

int HandleGoogleApiRequest(stream_t* p_access)
{
    auto p_sys = reinterpret_cast<access_sys_t*>(p_access->p_sys);
    p_sys->is_google_api_request = true;


    std::string url_str = p_access->psz_url;
    std::string access_token;

    size_t token_pos = url_str.find("access_token=");
    if (token_pos != std::string::npos) {
        size_t token_start = token_pos + strlen("access_token=");
        size_t token_end = url_str.find('&', token_start);
        std::string raw_token = url_str.substr(token_start, token_end - token_start);
        
        char *token_copy = strdup(raw_token.c_str());
        if (token_copy) {
            char *decoded = vlc_uri_decode(token_copy);
            if (decoded) {
                access_token = decoded;
            } else {
                access_token = raw_token; // fallback to raw token if decode fails
            }
            free(token_copy); // free the copy (decoded points to the same memory)
        } else {
            access_token = raw_token; // fallback if strdup fails
        }
    }

    if (access_token.empty()) {
        msg_Err(p_access, "No access_token found in Google API URL");
        return VLC_EGENERIC;
    }

    p_sys->access_token = access_token;

    std::string cleaned_url;
    if (token_pos != std::string::npos) {
        size_t token_start = token_pos;
        size_t token_end = url_str.find('&', token_pos + strlen("access_token="));

        if (token_end != std::string::npos) {
            // there are more parameters after access_token
            cleaned_url = url_str.substr(0, token_start) + url_str.substr(token_end + 1);
        } else {
            // access_token is the last parameter
            cleaned_url = url_str.substr(0, token_start);
            // remove trailing '?' or '&' if present
            if (!cleaned_url.empty() && (cleaned_url.back() == '?' || cleaned_url.back() == '&')) {
                cleaned_url.pop_back();
        }
        }
    } else {
        cleaned_url = url_str;
    }

    p_sys->base_url = cleaned_url;

    if (StartRequest(p_access, 0) != VLC_SUCCESS)
        return VLC_EGENERIC;

    if (ParseResponse(p_access) != VLC_SUCCESS)
        return VLC_EGENERIC;

    p_access->pf_block = GoogleApiBlock;
    p_access->pf_control = GoogleApiControl;
    p_access->pf_seek = GoogleApiSeek;

    p_sys->i_pos = 0;

    msg_Info(p_access, "Successfully established Google API connection using libhttp");
    return VLC_SUCCESS;
}

static int StartRequest(stream_t *p_access, uint64_t range_from)
{
    auto p_sys = reinterpret_cast<access_sys_t*>(p_access->p_sys);

    vlc_url_t url;
    if (vlc_UrlParse(&url, p_sys->base_url.c_str()) != VLC_SUCCESS) {
        msg_Err(p_access, "Invalid URL: %s", p_sys->base_url.c_str());
        return VLC_EGENERIC;
    }

    if (!url.psz_host || !url.psz_protocol) {
        msg_Err(p_access, "No host or protocol in URL: %s", p_sys->base_url.c_str());
        vlc_UrlClean(&url);
        return VLC_EGENERIC;
    }

    bool https = strcmp(url.psz_protocol, "https") == 0;
    if (!https) {
        msg_Err(p_access, "Google API requires HTTPS");
        vlc_UrlClean(&url);
        return VLC_EGENERIC;
    }

    unsigned port = url.i_port ? url.i_port : 443;
    std::string host = url.psz_host;

    std::string path = url.psz_path ? url.psz_path : "/";
    if (url.psz_option) {
        path += "?";
        path += url.psz_option;
    }

    if (p_sys->http_mgr == NULL) {
        p_sys->http_mgr = vlc_http_mgr_create(VLC_OBJECT(p_access), NULL);
        if (!p_sys->http_mgr) {
            msg_Err(p_access, "Failed to create HTTP manager");
            vlc_UrlClean(&url);
            return VLC_EGENERIC;
        }
    }

    char *authority = vlc_http_authority(url.psz_host, port);
    if (!authority) {
        vlc_UrlClean(&url);
        return VLC_ENOMEM;
    }

    struct vlc_http_msg *req = vlc_http_req_create("GET", url.psz_protocol, authority, path.c_str());
    free(authority);
    vlc_UrlClean(&url);
    if (!req)
        return VLC_ENOMEM;

    vlc_http_msg_add_header(req, "Authorization", "Bearer %s", p_sys->access_token.c_str());
    vlc_http_msg_add_header(req, "Accept", "*/*");

    if (range_from > 0)
        vlc_http_msg_add_header(req, "Range", "bytes=%" PRIu64 "-", range_from);

    char *ua = var_InheritString(p_access, "http-user-agent");
    if (ua && *ua)
        vlc_http_msg_add_agent(req, ua);
    else
        vlc_http_msg_add_agent(req, "VLC media player");
    free(ua);

    if (p_sys->http_resp) {
        vlc_http_msg_destroy(p_sys->http_resp);
        p_sys->http_resp = NULL;
    }

    struct vlc_http_msg *resp = vlc_http_mgr_request(p_sys->http_mgr, https, host.c_str(), port, req, false, false);

    vlc_http_msg_destroy(req);

    if (!resp) {
        msg_Err(p_access, "Failed to send HTTP request");
        return VLC_EGENERIC;
    }

    p_sys->http_resp = resp;
    return VLC_SUCCESS;
}

static int ParseResponse(stream_t *p_access)
{
    auto p_sys = reinterpret_cast<access_sys_t*>(p_access->p_sys);
    if (!p_sys->http_resp)
        return VLC_EGENERIC;

    struct vlc_http_msg *orig = p_sys->http_resp;
    struct vlc_http_msg *final = vlc_http_msg_get_final(orig);
    if (!final) {
        msg_Err(p_access, "Failed to get final HTTP response");
        vlc_http_msg_destroy(orig);
        p_sys->http_resp = NULL;
        return VLC_EGENERIC;
    }

    if (final != orig)
        vlc_http_msg_destroy(orig);
    p_sys->http_resp = final;

    int status_code = vlc_http_msg_get_status(p_sys->http_resp);

    if (status_code == 301 || status_code == 302 || status_code == 307 || status_code == 308) {
        const char *location = vlc_http_msg_get_header(p_sys->http_resp, "Location");
        if (location) {
            vlc_url_t redirect_url = {};
            if (vlc_UrlParse(&redirect_url, location) == VLC_SUCCESS && redirect_url.psz_host) {
                if (IsGoogleDomain(redirect_url.psz_host)) {
                    msg_Info(p_access, "Following redirect to %s", location);
                    p_sys->base_url = location;
                    vlc_UrlClean(&redirect_url);
                    vlc_http_msg_destroy(p_sys->http_resp);
                    p_sys->http_resp = NULL;
                    return (StartRequest(p_access, 0) == VLC_SUCCESS) ? ParseResponse(p_access) : VLC_EGENERIC;
                } else {
                    msg_Err(p_access, "Redirect to non-Google domain blocked for security: %s", location);
                }
            }
            vlc_UrlClean(&redirect_url);
        }
        msg_Err(p_access, "HTTP redirect without valid Location header");
        vlc_http_msg_destroy(p_sys->http_resp);
        p_sys->http_resp = NULL;
        return VLC_EGENERIC;
    }
    
    if (status_code == 401) {
        msg_Err(p_access, "HTTP 401 Unauthorized. access token may be expired or invalid");
        return VLC_EGENERIC;
    }
    if (status_code == 403) {
        msg_Err(p_access, "HTTP 403 Forbidden. access token may lack required permissions");
        return VLC_EGENERIC;
    }
    if (status_code != 200 && status_code != 206) {
        msg_Err(p_access, "HTTP server responded with status %d", status_code);
        return VLC_EGENERIC;
    }

    uintmax_t total = vlc_http_msg_get_size(p_sys->http_resp);
    if (total != (uintmax_t)-1)
        p_sys->i_size = (uint64_t)total;
    else {
        p_sys->i_size = UINT64_MAX;
    }

    const char *cr = vlc_http_msg_get_header(p_sys->http_resp, "Content-Range");
    if (cr) {
        const char *slash = strchr(cr, '/');
        if (slash && slash[1] && slash[1] != '*') {
            uint64_t total2 = strtoull(slash + 1, NULL, 10);
            if (total2 > 0)
                p_sys->i_size = total2;
        }
    }

    const char *accept_ranges = vlc_http_msg_get_header(p_sys->http_resp, "Accept-Ranges");
    p_sys->b_seekable = accept_ranges && strcasestr(accept_ranges, "bytes");

    if (!p_sys->b_seekable && p_sys->i_size != UINT64_MAX) // keep previous behavior
        p_sys->b_seekable = true;

    msg_Info(p_access, "HTTP connection established (status: %d, size: %llu, seekable: %s)",
             status_code, (unsigned long long)p_sys->i_size, p_sys->b_seekable ? "yes" : "no");

    return VLC_SUCCESS;
}

static block_t *GoogleApiBlock(stream_t *p_access, bool *eof)
{
    auto p_sys = reinterpret_cast<access_sys_t*>(p_access->p_sys);
    if (eof) *eof = false;
    if (!p_sys || !p_sys->http_resp) {
        if (eof) *eof = true;
        return NULL;
    }

    block_t *b = vlc_http_msg_read(p_sys->http_resp);
    if (b == vlc_http_error) {
        if (eof) *eof = true;
        return NULL;
    }
    if (!b) { // end of stream
        if (eof) *eof = true;
        return NULL;
    }

    p_sys->i_pos += b->i_buffer;
    return b;
}

static int GoogleApiSeek(stream_t *p_access, uint64_t i_pos)
{
    auto p_sys = reinterpret_cast<access_sys_t*>(p_access->p_sys);

    if (!p_sys || !p_sys->b_seekable) {
        return VLC_EGENERIC;
    }
    if (p_sys->i_pos == i_pos) {
        return VLC_SUCCESS;
    }
    if (p_sys->i_size != UINT64_MAX && i_pos >= p_sys->i_size)
        return VLC_EGENERIC;

    if (StartRequest(p_access, i_pos) != VLC_SUCCESS)
        return VLC_EGENERIC;
    if (ParseResponse(p_access) != VLC_SUCCESS)
        return VLC_EGENERIC;

    p_sys->i_pos = i_pos;
    return VLC_SUCCESS;
}

static int GoogleApiControl(stream_t *p_access, int i_query, va_list args)
{
    auto p_sys = reinterpret_cast<access_sys_t*>(p_access->p_sys);
    if (!p_sys) return VLC_EGENERIC;

    switch (i_query) {
        case STREAM_CAN_SEEK:
            *va_arg(args, bool *) = p_sys->b_seekable;
            return VLC_SUCCESS;
        case STREAM_GET_SIZE:
            if (p_sys->i_size != UINT64_MAX) {
                *va_arg(args, uint64_t *) = p_sys->i_size;
                return VLC_SUCCESS;
            }
            return VLC_EGENERIC;
        case STREAM_CAN_PAUSE:
        case STREAM_CAN_CONTROL_PACE:
             *va_arg(args, bool *) = true;
            return VLC_SUCCESS;
        case STREAM_CAN_FASTSEEK:
            *va_arg(args, bool *) = false;
            return VLC_SUCCESS;
        case STREAM_GET_PTS_DELAY:
            *va_arg(args, vlc_tick_t *) = VLC_TICK_FROM_MS(
                var_InheritInteger(p_access, "network-caching"));
            return VLC_SUCCESS;
        default:
            return VLC_EGENERIC;
    }
}

static bool ends_with_ci(const char *host, const char *suffix)
{
    size_t hl = strlen(host), sl = strlen(suffix);
    return hl >= sl && strncasecmp(host + hl - sl, suffix, sl) == 0;
}

static bool IsGoogleDomain(const char *host)
{
    if (!host)
        return false;
    
    if (!strcasecmp(host, "googleapis.com") ||
        !strcasecmp(host, "googleusercontent.com") ||
        !strcasecmp(host, "google.com"))
        return true;
    
    if (ends_with_ci(host, ".googleapis.com") ||
        ends_with_ci(host, ".googleusercontent.com") ||
        ends_with_ci(host, ".google.com"))
        return true;
    
    return false;
}

