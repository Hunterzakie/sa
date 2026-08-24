/*****************************************************************************
 * services_discovery.cpp: cloud storage services discovery module
 *****************************************************************************
 * Copyright (C) 2017 VideoLabs and VideoLAN
 *
 * Authors: Maksym Yemelianenko <max.yemelianenko@gmail.com>
 *          Ashish Vardhan Karanam <ashishkaranam06@gmail.com>
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
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <vlc_keystore.h>
#include <vlc_url.h>
#include <vlc_input_item.h>
#include <vlc_interrupt.h>
#include <vlc_threads.h>
#include <vlc_variables.h>

#include <cloudstorage/ICloudStorage.h>
#include <cloudstorage/ICloudProvider.h>
#include <cloudstorage/IItem.h>
#include <algorithm>

#include "services_discovery.h"
#include "token_cache.h"
#include "vlc_http.h"
#include "vlc_httpd_server.h"
#include "media_filter.h"
#include "cloudstorage_common.h"

#if __has_include("cloud_keys.h")
#include "cloud_keys.h"
#endif

using cloudstorage::ICloudProvider;
using cloudstorage::ICloudStorage;

class SDAuthCallback final : public cloudstorage::ICloudProvider::IAuthCallback {
public:
    explicit SDAuthCallback(vlc_object_t* sd) : p_sd(sd) {}
    Status userConsentRequired(const cloudstorage::ICloudProvider& provider) override {
        msg_Info(p_sd, "=== Authentication Required for %s ===", provider.name().c_str());
        msg_Info(p_sd, "AUTHORIZATION URL: %s", provider.authorizeLibraryUrl().c_str());
        return Status::WaitForAuthorizationCode;
    }
    void done(const cloudstorage::ICloudProvider& provider, cloudstorage::EitherError<void> error) override {
        if (!error.left()) {
            msg_Info(p_sd, "Authentication successful for %s!", provider.name().c_str());
        } else {
            msg_Err(p_sd, "Authentication failed for %s", provider.name().c_str());
        }
    }
private:
    vlc_object_t* p_sd;
};


template<typename F, typename Obj, typename... Args>
static auto SD_WrapAsync(F f, Obj&& obj, Args&&... args)
{
    ScopedSemaphore sem;
    auto finish = [&sem](auto){ sem.post(); };
    decltype((std::forward<Obj>(obj).*f)(std::forward<Args>(args)..., finish)) req;
    try {
        req = (std::forward<Obj>(obj).*f)(std::forward<Args>(args)..., finish);
    } catch (...) {
        return decltype(req->result().right())();
    }
    if (!req)
        return decltype(req->result().right())();
    sem.wait();
    decltype(req->result()) outcome;
    try { outcome = req->result(); } catch (...) { return decltype(outcome.right())(); }
    if (outcome.left())
        return decltype(outcome.right())();
    return outcome.right();
}

static void SD_AddChild(services_discovery_t* p_sd, input_item_t* parent,
                        const std::string& base_url, const std::string& name,
                        bool is_dir)
{
    std::string url = base_url;
    if (!url.empty() && url.back() != '/') url.push_back('/');
    char* enc = vlc_uri_encode(name.c_str());
    if (!enc) return;
    url += enc;
    free(enc);

    input_item_t* child = nullptr;
    if (is_dir) {
        child = input_item_NewDirectory(url.c_str(), name.c_str(), ITEM_NET);
        if (child)
            input_item_AddOption(child, "no-auto-preparse", VLC_INPUT_OPTION_TRUSTED);
    } else {
        child = input_item_NewFile(url.c_str(), name.c_str(), INPUT_DURATION_UNSET, ITEM_NET);
    }
    if (!child) return;
    input_item_SetMeta(child, vlc_meta_Title, name.c_str());
    services_discovery_AddSubItem(p_sd, parent, child);
}

static void SD_PopulateProviderRoot(services_discovery_t* p_sd, input_item_t* parent)
{
    if (!parent || !parent->psz_name) return;
    vlc_url_t u; vlc_UrlParse(&u, parent->psz_name);
    const char* host_c = u.psz_host;
    const char* path_c = u.psz_path ? u.psz_path : "/";
    if (!host_c || !*host_c) { vlc_UrlClean(&u); return; }
    std::string host(host_c);

    vlc_url_t keyu; memset(&keyu, 0, sizeof(keyu));
    keyu.psz_protocol = (char*)"https";
    keyu.psz_host = (char*)host_c;
    std::string user, token; unsigned found = 0;
    keyu.psz_username = (char*)"default";
    std::string tok = TokenCache::get(VLC_OBJECT(p_sd), &keyu);
    if (!tok.empty()) {
        user = "default";
        token = std::move(tok);
        found = 1;
    }

    if (found != 1) { vlc_UrlClean(&u); return; }

    cloudstorage::ICloudProvider::InitData init;
    init.token_ = token;
    init.permission_ = cloudstorage::ICloudProvider::Permission::ReadWrite;
    init.callback_ = std::make_shared<SDAuthCallback>(VLC_OBJECT(p_sd));

    char *psz_client_id = nullptr;
    char *psz_client_secret = nullptr;
    if (host == "google" || host == "googledrive" || host == "dropbox" ||
        host == "onedrive" || host == "box" || host == "pcloud") {
        std::string target = (host == "googledrive") ? "google" : host;
        std::string opt_id = "cloudstorage-" + target + "-client-id";
        std::string opt_secret = "cloudstorage-" + target + "-client-secret";
        psz_client_id = var_InheritString(p_sd, opt_id.c_str());
        psz_client_secret = var_InheritString(p_sd, opt_secret.c_str());
    }

    if (psz_client_id && *psz_client_id)
        init.hints_["client_id"] = psz_client_id;
    if (psz_client_secret && *psz_client_secret)
        init.hints_["client_secret"] = psz_client_secret;

    free(psz_client_id);
    free(psz_client_secret);

    // Fallback to compile-time keys
    if (init.hints_["client_id"].empty()) {
#ifdef HAS_GOOGLE_DRIVE_KEY
        if (host == "google" || host == "googledrive") {
            init.hints_["client_id"] = GOOGLE_DRIVE_APP_ID;
            init.hints_["client_secret"] = GOOGLE_DRIVE_APP_SECRET;
        }
#endif
#ifdef HAS_DROPBOX_KEY
        if (host == "dropbox") {
            init.hints_["client_id"] = DROPBOX_APP_ID;
            init.hints_["client_secret"] = DROPBOX_APP_SECRET;
        }
#endif
#ifdef HAS_ONEDRIVE_KEY
        if (host == "onedrive") {
            init.hints_["client_id"] = ONEDRIVE_APP_ID;
            init.hints_["client_secret"] = ONEDRIVE_APP_SECRET;
        }
#endif
#ifdef HAS_BOX_KEY
        if (host == "box") {
            init.hints_["client_id"] = BOX_APP_ID;
            init.hints_["client_secret"] = BOX_APP_SECRET;
        }
#endif
#ifdef HAS_PCLOUD_KEY
        if (host == "pcloud") {
            init.hints_["client_id"] = PCLOUD_APP_ID;
            init.hints_["client_secret"] = PCLOUD_APP_SECRET;
        }
#endif
    }

    init.http_engine_ = std::make_unique<vlc::cloudstorage::VLCHttp>(VLC_OBJECT(p_sd));
    init.http_server_ = std::make_unique<vlc::cloudstorage::VLCHttpServerFactory>(VLC_OBJECT(p_sd));

    cloudstorage::ICloudProvider::Pointer prov;
    try {
        prov = cloudstorage::ICloudStorage::create()->provider(host, std::move(init));
    } catch (...) { prov.reset(); }
    if (!prov) { vlc_UrlClean(&u); return; }

    std::string path = path_c;
    auto dir = (path == "/") ? prov->rootDirectory() : SD_WrapAsync(&cloudstorage::ICloudProvider::getItemAsync, *prov, path);
    if (!dir) { vlc_UrlClean(&u); return; }

    auto list = SD_WrapAsync(&cloudstorage::ICloudProvider::listDirectorySimpleAsync, *prov, dir);
    if (!list) {
        msg_Err(p_sd, "SD_PopulateProviderRoot: listDirectorySimpleAsync returned null");
        vlc_UrlClean(&u); 
        return; 
    }
    
    msg_Info(p_sd, "=== SD SUCCESS: Found %zu items in directory ===", list->size());
    
    std::string base = std::string("cloudstorage://") + host + path;
    for (auto& itm : *list) {
        bool is_dir = itm->type() == cloudstorage::IItem::FileType::Directory;
        if (!vlc::cloudstorage::IsSupportedMediaFile(itm)) {
            msg_Dbg(p_sd, "SD Skipping non-media item: %s", itm->filename().c_str());
            continue;
        }
        msg_Info(p_sd, "SD Adding item: %s", itm->filename().c_str());
        SD_AddChild(p_sd, parent, base, itm->filename(), is_dir);
    }
    vlc_UrlClean(&u);
}

static int GetProvidersList( services_discovery_t * );
static int InsertNewUserInput( services_discovery_t *, input_item_t * );

int SDOpen( vlc_object_t *p_this )
{
    services_discovery_t *p_sd = (services_discovery_t *) p_this;
    services_discovery_sys_t *p_sys;

    p_sd->p_sys = p_sys = new services_discovery_sys_t();
    if ( p_sys == nullptr)
        return VLC_ENOMEM;
    p_sys->auth_progress = false;
    p_sys->auth_item = nullptr;
    p_sys->workers.clear();
    p_sd->description = "Cloud Storage";

    if ( GetProvidersList( p_sd ) != VLC_SUCCESS )
        goto error;

    for ( const auto &prov : p_sys->providers_list )
    {
        std::string uri = std::string("cloudstorage://") + prov + "/";
         if (p_sys->providers_items.find(uri) != p_sys->providers_items.end())
             continue;

         input_item_t *it = input_item_NewDirectory( uri.c_str(), uri.c_str(), ITEM_NET );
         if ( it )
         {
             input_item_SetMeta(it, vlc_meta_Title, prov.c_str());
             input_item_AddOption(it, "no-auto-preparse", VLC_INPUT_OPTION_TRUSTED);
             InsertNewUserInput( p_sd, it );
         }
     }

    return VLC_SUCCESS;

error:
    SDClose( p_this );
    return VLC_EGENERIC;
}

void SDClose( vlc_object_t *p_this )
{
    services_discovery_t *p_sd = (services_discovery_t *) p_this;
    services_discovery_sys_t *p_sys = (services_discovery_sys_t *) p_sd->p_sys;

    for (size_t i = 0; i < p_sys->workers.size(); ++i)
        vlc_join(p_sys->workers[i], nullptr);
    p_sys->workers.clear();

    for ( auto& p_item_root : p_sys->providers_items )
        delete p_item_root.second;
    if ( p_sys->auth_item != nullptr )
        delete p_sys->auth_item;

    delete p_sys;
}

static int GetProvidersList( services_discovery_t * p_sd )
{
    services_discovery_sys_t *p_sys = (services_discovery_sys_t *) p_sd->p_sys;
    try {
        for ( const auto& provider : ICloudStorage::create()->providers() ) {
            msg_Info( p_sd, "Found libcloudstorage provider: %s", provider.c_str() );
            p_sys->providers_list.push_back( provider );
        }
    } catch (const std::exception& e) {
        msg_Err( p_sd, "Exception while loading providers: %s", e.what() );
    } catch (...) {
        msg_Err( p_sd, "Unknown exception while loading providers" );
    }

    if ( p_sys->providers_list.empty() )
    {
        msg_Err( p_sd, "Failed to load providers from libcloudstorage" );
        return VLC_EGENERIC;
    }
    return VLC_SUCCESS;
}

static int InsertNewUserInput( services_discovery_t *p_sd, input_item_t *item )
{
    if (!item) return VLC_EGENERIC;
    services_discovery_AddItem(p_sd, item);
    auto *prov = new provider_item_t(item, /* thread */ nullptr);
    ((services_discovery_sys_t*)p_sd->p_sys)
        ->providers_items[item->psz_name] = prov;
    return VLC_SUCCESS;
}

provider_item_t::provider_item_t(input_item_t *item, input_thread_t *)
    : item(item), thread(nullptr) {}

provider_item_t::~provider_item_t()
{
    input_item_Release(item);
}
