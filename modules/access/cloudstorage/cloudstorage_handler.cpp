/*****************************************************************************
 * cloudstorage_handler.cpp: Implementation for libcloudstorage
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
#include "cloudstorage_handler.h"
#include "provider_callback.h"
#include "token_cache.h"

#include <vlc_common.h>
#include <vlc_stream.h>
#include <vlc_input.h>
#include <vlc_url.h>
#include <vlc_keystore.h>
#include <vlc_interrupt.h>

#include <cloudstorage/ICloudStorage.h>

#include <string>

using cloudstorage::ICloudStorage;
using cloudstorage::ICloudProvider;
using cloudstorage::IItem;

// RAII wrapper for VLC semaphores
struct ScopedSemaphore {
    vlc_sem_t sem;
    ScopedSemaphore() { vlc_sem_init(&sem, 0); }
    ~ScopedSemaphore() { /* no-op */ }
    void post() { vlc_sem_post(&sem); }
    void wait() { vlc_sem_wait_i11e(&sem); }
};

class NoPromptCallback final : public cloudstorage::ICloudProvider::IAuthCallback {
public:
    explicit NoPromptCallback(stream_t*) {}
    Status userConsentRequired(const cloudstorage::ICloudProvider&) override {
        return Status::None;
    }
    void done(const cloudstorage::ICloudProvider&, cloudstorage::EitherError<void>) override {}
};

class AuthRouteCallback final : public cloudstorage::ICloudProvider::IAuthCallback {
public:
    explicit AuthRouteCallback(stream_t* access)
        : p_access(access), p_sys(reinterpret_cast<access_sys_t*>(access->p_sys)) {}

    Status userConsentRequired(const cloudstorage::ICloudProvider& provider) override {
        msg_Info(p_access, "=== Authentication Required for %s ===", provider.name().c_str());

        std::string url = provider.authorizeLibraryUrl();
        std::ostringstream question;
        question << "To access your " << provider.name()
                 << " account, please authorize VLC.\n"
                    "Authorization URL:\n" << url << "\n"
                    "Open in browser or handle manually?";

        int choice = vlc_dialog_wait_question(
            p_access,
            VLC_DIALOG_QUESTION_NORMAL,
            "Cancel", "Open Browser", "Manual",
            "Cloud Storage Authentication",
            "%s", question.str().c_str());

        switch (choice) {
            case 0:
                msg_Info(p_access, "User cancelled authentication");
                return Status::None;
            case 1:
                openInBrowser(url);
                break;
            case 2:
            default:
                msg_Info(p_access, "User will handle authentication manually");
                break;
        }

        msg_Info(p_access, "Waiting for authorization code...");
        return Status::WaitForAuthorizationCode;
    }

    void done(const cloudstorage::ICloudProvider& provider,
              cloudstorage::EitherError<void> error) override {
        if (error.left()) {
            msg_Err(p_access, "Authorization Error %d: %s",
                    error.left()->code_, error.left()->description_.c_str());
            p_sys->authenticated = false;
        } else {
            msg_Info(p_access, "Authentication successful for %s!", provider.name().c_str());
            p_sys->authenticated = true;
            p_sys->token = provider.token();
            msg_Dbg(p_access, "Token length: %zu", p_sys->token.length());
        }
    }

private:
    void openInBrowser(const std::string& url) const {
#ifdef __APPLE__
        const char* opener = "open";
#elif defined(__linux__)
        const char* opener = "xdg-open";
#elif defined(_WIN32)
        const char* opener = "start";
#else
        const char* opener = "";
#endif
        if (!*opener) {
            msg_Warn(p_access, "Cannot open browser automatically, please visit: %s", url.c_str());
            return;
        }
        std::string cmd = std::string(opener) + ' ' + url;
        if (std::system(cmd.c_str()) != 0)
            msg_Warn(p_access, "Failed to launch browser, open manually: %s", url.c_str());
        else
            msg_Info(p_access, "Opened browser for URL: %s", url.c_str());
    }

    stream_t* p_access;
    access_sys_t* p_sys;
};

static int GetCredentials(stream_t* p_access);
static int InitProvider(stream_t* p_access);
static int AddItem(input_item_node_t* p_node, stream_t* p_access, IItem::Pointer item);

template<typename F, typename Obj, typename... Args>
static auto WrapAsync(stream_t* p_access, F f, Obj&& obj, Args&&... args) {
    ScopedSemaphore sem;
    auto finish = [&sem](auto) { sem.post(); };
    decltype((std::forward<Obj>(obj).*f)(std::forward<Args>(args)..., finish)) req;
    try {
        req = (std::forward<Obj>(obj).*f)(std::forward<Args>(args)..., finish);
    } catch (...) {
        msg_Err(p_access, "Cloud request threw before start");
        return decltype(req->result().right())();
    }
    if (!req) {
        msg_Err(p_access, "Cloud request failed to start (null request)");
        return decltype(req->result().right())();
    }

    sem.wait();

    decltype(req->result()) outcome;
    try {
        outcome = req->result();
    } catch (...) {
        msg_Err(p_access, "Cloud request result threw");
        return decltype(outcome.right())();
    }

    if (outcome.left()) {
        auto p_sys = reinterpret_cast<access_sys_t*>(p_access->p_sys);
        msg_Err(p_access, "Cloud request failed (%d): %s",
                outcome.left()->code_, outcome.left()->description_.c_str());
        if (outcome.left()->code_ == 401 || outcome.left()->code_ == 403) {
            msg_Info(p_access, "Authentication failed, clearing stored token");
            p_sys->token.clear();
            p_sys->authenticated = false;
            TokenCache::clear(p_access, &p_sys->url);
        }
        return decltype(outcome.right())();
    }

    auto p_sys = reinterpret_cast<access_sys_t*>(p_access->p_sys);
    if (!p_sys->token.empty()) {
        p_sys->authenticated = true;
        TokenCache::set(p_access, &p_sys->url, p_sys->token);
    }
    return outcome.right();
}

int HandleCloudProviderRequest(stream_t* p_access)
{
    msg_Dbg(p_access, "HandleCloudProviderRequest: url=%s", p_access->psz_url ? p_access->psz_url : "(null)");
    {
        auto p_sys = reinterpret_cast<access_sys_t*>(p_access->p_sys);
        std::string host = p_sys->url.psz_host ? p_sys->url.psz_host : "";
        if (host == "auth") {
            const char* path = p_sys->url.psz_path ? p_sys->url.psz_path : "/";
            std::string path_str = path;
            if (!path_str.empty() && path_str.front() == '/') path_str.erase(0, 1);
            std::string provider_name;
            size_t slash = path_str.find('/');
            provider_name = (slash == std::string::npos) ? path_str : path_str.substr(0, slash);
            if (provider_name.empty()) {
                return VLC_EGENERIC;
            }

            ICloudProvider::InitData init{ std::string(), ICloudProvider::Permission::ReadWrite,
                std::make_shared<AuthRouteCallback>(p_access), nullptr };
            cloudstorage::ICloudProvider::Pointer prov;
            try {
                prov = ICloudStorage::create()->provider(provider_name.c_str(), std::move(init));
            } catch (...) { prov.reset(); }
            if (!prov) {
                return VLC_EGENERIC;
            }
            cloudstorage::IItem::Pointer root = prov->rootDirectory();
            if (!root)
                root = WrapAsync(p_access, &ICloudProvider::getItemAsync, *prov, "/");
            if (root)
                (void)WrapAsync(p_access, &ICloudProvider::listDirectorySimpleAsync, *prov, root);

            std::string token = prov->token();
            if (!token.empty()) {
                vlc_url_t key = p_sys->url;
                key.psz_host = const_cast<char*>(provider_name.c_str());
                TokenCache::set(p_access, &key, token);
            }

            p_access->psz_url = strdup("vlc://nop");
            return VLC_ACCESS_REDIRECT;
        }
    }
    int err;
    if ((err = GetCredentials(p_access)) != VLC_SUCCESS) return err;
    if ((err = InitProvider(p_access)) != VLC_SUCCESS) return err;

    auto p_sys = reinterpret_cast<access_sys_t*>(p_access->p_sys);

    if (p_sys->current_item && p_sys->current_item->type() == IItem::FileType::Directory) {
        const bool had_token = !p_sys->token.empty();
        auto probe = WrapAsync(p_access,
            &ICloudProvider::listDirectorySimpleAsync, *p_sys->provider,
            p_sys->current_item);
        if (!probe) {
            if (had_token && !p_access->b_preparsing) {
                std::string auth_mrl = "cloudstorage://";
                if (p_sys->url.psz_username && *p_sys->url.psz_username) {
                    auth_mrl += p_sys->url.psz_username;
                    auth_mrl += "@";
                }
                auth_mrl += "auth/";
                if (p_sys->url.psz_host && *p_sys->url.psz_host)
                    auth_mrl += p_sys->url.psz_host;
                auth_mrl += "/";
                msg_Info(p_access, "Token invalid for %s; redirecting to %s for re-authentication",
                         p_sys->url.psz_host ? p_sys->url.psz_host : "(null)", auth_mrl.c_str());
                p_access->psz_url = strdup(auth_mrl.c_str());
                if (!p_access->psz_url)
                    return VLC_ENOMEM;
                return VLC_ACCESS_REDIRECT;
            }
            return VLC_EGENERIC;
        }

        p_access->pf_control = access_vaDirectoryControlHelper;
        p_access->pf_readdir = ReadDir;
        return VLC_SUCCESS;
    }

    const bool had_token = !p_sys->token.empty();
    auto urlptr = WrapAsync(p_access,
        &ICloudProvider::getItemUrlAsync, *p_sys->provider,
        p_sys->current_item);

    if (!urlptr) {
        if (had_token && !p_access->b_preparsing) {
            std::string auth_mrl = "cloudstorage://";
            if (p_sys->url.psz_username && *p_sys->url.psz_username) {
                auth_mrl += p_sys->url.psz_username;
                auth_mrl += "@";
            }
            auth_mrl += "auth/";
            if (p_sys->url.psz_host && *p_sys->url.psz_host)
                auth_mrl += p_sys->url.psz_host;
            auth_mrl += "/";
            msg_Info(p_access, "Token invalid for %s; redirecting to %s for re-authentication",
                     p_sys->url.psz_host ? p_sys->url.psz_host : "(null)", auth_mrl.c_str());
            p_access->psz_url = strdup(auth_mrl.c_str());
            if (!p_access->psz_url)
                return VLC_ENOMEM;
            return VLC_ACCESS_REDIRECT;
        }
        return VLC_EGENERIC;
    }

    p_access->psz_url = strdup(urlptr->c_str());
    return VLC_ACCESS_REDIRECT;
}

static int GetCredentials(stream_t* p_access)
{
    auto p_sys = reinterpret_cast<access_sys_t*>(p_access->p_sys);
    const char* user = p_sys->url.psz_username ? p_sys->url.psz_username : "(null)";
    const char* host = p_sys->url.psz_host ? p_sys->url.psz_host : "(null)";
    msg_Dbg(p_access, "Looking for creds %s@%s", user, host);

    std::string tok = TokenCache::get(p_access, &p_sys->url);
    if (!tok.empty()) {
        p_sys->token = tok;
        p_sys->authenticated = false;
        return VLC_SUCCESS;
    }

    bool is_background = p_access->b_preparsing;

    if (is_background) {
        msg_Info(p_access, "No token for %s in background; refusing to open", host);
        return VLC_EGENERIC;
    }

    std::string auth_mrl = "cloudstorage://";
    if (p_sys->url.psz_username && *p_sys->url.psz_username) {
        auth_mrl += p_sys->url.psz_username;
        auth_mrl += "@";
    }
    auth_mrl += "auth/";
    if (p_sys->url.psz_host && *p_sys->url.psz_host)
        auth_mrl += p_sys->url.psz_host;
    auth_mrl += "/";
    msg_Info(p_access, "No token for %s; redirecting to %s for authentication", host, auth_mrl.c_str());
    p_access->psz_url = strdup(auth_mrl.c_str());
    if (!p_access->psz_url)
        return VLC_ENOMEM;
    return VLC_ACCESS_REDIRECT;
}

static int InitProvider(stream_t* p_access)
{
    auto p_sys = reinterpret_cast<access_sys_t*>(p_access->p_sys);
    if (!p_sys->url.psz_host) {
        msg_Err(p_access, "No provider host in URL");
        return VLC_EGENERIC;
    }
    ICloudProvider::InitData init{ p_sys->token, ICloudProvider::Permission::ReadWrite,
        std::make_shared<NoPromptCallback>(p_access), nullptr };
    try {
        p_sys->provider = ICloudStorage::create()->provider(p_sys->url.psz_host, std::move(init));
    } catch (...) {
        p_sys->provider.reset();
    }
    if (!p_sys->provider) {
        msg_Err(p_access, "Failed to create provider for host %s", p_sys->url.psz_host);
        return VLC_EGENERIC;
    }

    try {
        if (std::string(p_sys->url.psz_path ? p_sys->url.psz_path : "/") == "/") {
            if (auto root = p_sys->provider->rootDirectory())
                p_sys->current_item = root;
            else
                p_sys->current_item = WrapAsync(p_access,
                    &ICloudProvider::getItemAsync, *p_sys->provider, "/");
        } else {
            const char* dec = vlc_uri_decode(p_sys->url.psz_path);
            p_sys->current_item = WrapAsync(p_access,
                &ICloudProvider::getItemAsync, *p_sys->provider,
                dec ? dec : p_sys->url.psz_path);
        }
    } catch (...) {
        p_sys->current_item.reset();
        msg_Err(p_access, "Exception while resolving current item");
        return VLC_EGENERIC;
    }
    if (!p_sys->current_item) {
        msg_Err(p_access, "Failed to resolve current item");
        return VLC_EGENERIC;
    }
    return VLC_SUCCESS;
}

int ReadDir(stream_t* p_access, input_item_node_t* p_node)
{
    auto p_sys = reinterpret_cast<access_sys_t*>(p_access->p_sys);
    if (!p_sys->provider || !p_sys->current_item) {
        msg_Err(p_access, "ReadDir without initialized provider/current_item");
        return VLC_EGENERIC;
    }
    auto list = WrapAsync(p_access,
        &ICloudProvider::listDirectorySimpleAsync, *p_sys->provider,
        p_sys->current_item);
    if (!list) return VLC_EGENERIC;
    for (auto& itm : *list)
        if (AddItem(p_node, p_access, itm) != VLC_SUCCESS)
            return VLC_EGENERIC;
    return VLC_SUCCESS;
}

static int AddItem(input_item_node_t* p_node, stream_t* p_access, IItem::Pointer item)
{
    std::string url = p_access->psz_url ? p_access->psz_url : "";
    if (url.empty())
        url = "cloudstorage://"; // fallback to scheme to avoid empty URLs
    if (url.back() != '/')
        url.push_back('/');

    char* enc = vlc_uri_encode(item->filename().c_str());
    if (!enc)
        return VLC_ENOMEM;
    url += enc;
    free(enc);

    input_item_t* i = (item->type()==IItem::FileType::Directory)
        ? input_item_NewDirectory(url.c_str(), item->filename().c_str(), ITEM_NET)
        : input_item_NewFile(url.c_str(), item->filename().c_str(), INPUT_DURATION_UNSET, ITEM_NET);
    if (!i) return VLC_EGENERIC;
    auto nod = input_item_node_AppendItem(p_node, i);
    input_item_Release(i);
    return nod ? VLC_SUCCESS : VLC_EGENERIC;
}
