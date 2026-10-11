/*****************************************************************************
 * services_discovery.cpp: cloud storage services discovery module
 *****************************************************************************
 * Copyright (C) 2017 VideoLabs and VideoLAN
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <vlc_keystore.h>
#include <vlc_url.h>
#include <vlc_input_item.h>
#include <vlc_interrupt.h>
#include <vlc_threads.h>

#include <cloudstorage/ICloudStorage.h>
#include <cloudstorage/ICloudProvider.h>
#include <cloudstorage/IItem.h>
#include <algorithm>

#include "services_discovery.h"
#include "token_cache.h"

using cloudstorage::ICloudProvider;
using cloudstorage::ICloudStorage;

class SDAuthCallback final : public cloudstorage::ICloudProvider::IAuthCallback {
public:
    explicit SDAuthCallback(vlc_object_t*) {}
    Status userConsentRequired(const cloudstorage::ICloudProvider&) override {
        return Status::None;
    }
    void done(const cloudstorage::ICloudProvider&, cloudstorage::EitherError<void>) override {}
};

struct ScopedSemaphoreSD {
    vlc_sem_t sem;
    ScopedSemaphoreSD() { vlc_sem_init(&sem, 0); }
    void post() { vlc_sem_post(&sem); }
    void wait() { vlc_sem_wait_i11e(&sem); }
};

template<typename F, typename Obj, typename... Args>
static auto SD_WrapAsync(F f, Obj&& obj, Args&&... args)
{
    ScopedSemaphoreSD sem;
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
    const char* host = u.psz_host;
    const char* path_c = u.psz_path ? u.psz_path : "/";
    if (!host || !*host) { vlc_UrlClean(&u); return; }

    vlc_url_t keyu; memset(&keyu, 0, sizeof(keyu));
    keyu.psz_protocol = (char*)"https";
    keyu.psz_host = (char*)host;
    vlc_credential cred; vlc_credential_init(&cred, &keyu);
    vlc_credential_get(&cred, p_sd, NULL, NULL, NULL, NULL);
    std::string user, token; unsigned found = 0;
    for (unsigned i = 0; i < cred.i_entries_count; ++i) {
        const char* usr = cred.p_entries[i].ppsz_values[KEY_USER];
        if (!usr) continue;
        keyu.psz_username = (char*)usr;
        std::string tok = TokenCache::get(VLC_OBJECT(p_sd), &keyu);
        if (!tok.empty()) { user = usr; token = std::move(tok); ++found; }
    }
    vlc_credential_clean(&cred);
    if (found != 1) { vlc_UrlClean(&u); return; }

    cloudstorage::ICloudProvider::InitData init{ token,
        cloudstorage::ICloudProvider::Permission::ReadWrite,
        std::make_shared<SDAuthCallback>(VLC_OBJECT(p_sd)), nullptr };
    cloudstorage::ICloudProvider::Pointer prov;
    try {
        prov = cloudstorage::ICloudStorage::create()->provider(host, std::move(init));
    } catch (...) { prov.reset(); }
    if (!prov) { vlc_UrlClean(&u); return; }

    std::string path = path_c;
    auto dir = (path == "/") ? prov->rootDirectory() : SD_WrapAsync(&cloudstorage::ICloudProvider::getItemAsync, *prov, path);
    if (!dir) { vlc_UrlClean(&u); return; }

    auto list = SD_WrapAsync(&cloudstorage::ICloudProvider::listDirectorySimpleAsync, *prov, dir);
    if (!list) { vlc_UrlClean(&u); return; }
    std::string base = std::string("cloudstorage://") + host + path;
    for (auto& itm : *list) {
        bool is_dir = itm->type() == cloudstorage::IItem::FileType::Directory;
        SD_AddChild(p_sd, parent, base, itm->filename(), is_dir);
    }
    vlc_UrlClean(&u);
}

static void* SD_WorkerThread(void* opaque)
{
    auto tuple = static_cast<std::pair<services_discovery_t*, input_item_t*>*>(opaque);
    services_discovery_t* sd = tuple->first;
    input_item_t* item = tuple->second;
    SD_PopulateProviderRoot(sd, item);
    delete tuple;
    return nullptr;
}

static int GetProvidersList( services_discovery_t * );
static int RepresentUsers( services_discovery_t * );
static input_item_t * GetNewUserInput( services_discovery_t *, const char *,
                     const char * );
static int InsertNewUserInput( services_discovery_t *, input_item_t * );
static char * GenerateUserIdentifier( services_discovery_t *, const char * );
static int CallbackAuthentication( vlc_object_t *, char const *,
                     vlc_value_t, vlc_value_t, void * );
static int CallbackRequestedFromUI( vlc_object_t *, char const *,
                     vlc_value_t, vlc_value_t, void * );

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

    if ( var_Create( vlc_object_instance(p_sd), "cloudstorage-auth", VLC_VAR_STRING ) != VLC_SUCCESS )
        goto error;
    var_AddCallback( vlc_object_instance(p_sd), "cloudstorage-auth",
            CallbackAuthentication, p_sd );

    if ( var_Create( vlc_object_parent(p_sd), "cloudstorage-request", VLC_VAR_STRING ) != VLC_SUCCESS )
        goto error;
    var_AddCallback( vlc_object_parent(p_sd), "cloudstorage-request",
            CallbackRequestedFromUI, p_sd );

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

    var_DelCallback( vlc_object_instance(p_sd), "cloudstorage-auth",
            CallbackAuthentication, p_sd );
    var_DelCallback( vlc_object_parent(p_sd), "cloudstorage-request",
            CallbackRequestedFromUI, p_sd );
    delete p_sys;
}

static int GetProvidersList( services_discovery_t * p_sd )
{
    services_discovery_sys_t *p_sys = (services_discovery_sys_t *) p_sd->p_sys;
    for ( const auto& provider : ICloudStorage::create()->providers() )
        p_sys->providers_list.push_back( provider );

    if ( p_sys->providers_list.empty() )
    {
        msg_Err( p_sd, "Failed to load providers from libcloudstorage" );
        return VLC_EGENERIC;
    }
    return VLC_SUCCESS;
}

static int RepresentUsers( services_discovery_t *p_sd )
{
    auto *p_sys = (services_discovery_sys_t*)p_sd->p_sys;
    p_sys->auth_progress = false;

    for (auto &prov : p_sys->providers_list)
    {
        vlc_url_t dummy; memset(&dummy, 0, sizeof(dummy));
        dummy.psz_protocol = (char*)"https";
        dummy.psz_host = (char*)prov.c_str();

        vlc_credential cred; vlc_credential_init(&cred, &dummy);
        vlc_credential_get(&cred, p_sd, NULL, NULL, NULL, NULL);
        for (unsigned i = 0; i < cred.i_entries_count; i++)
        {
            auto *item = GetNewUserInput(p_sd,
                cred.p_entries[i].ppsz_values[KEY_USER],
                prov.c_str());
            InsertNewUserInput(p_sd, item);
        }
        vlc_credential_clean(&cred);
    }
    return VLC_SUCCESS;
}

static input_item_t *GetNewUserInput(
    services_discovery_t *p_sd, const char *user, const char *provider)
{
    auto *p_sys = (services_discovery_sys_t*)p_sd->p_sys;
    if (std::find(p_sys->providers_list.begin(),
                  p_sys->providers_list.end(), provider)
        == p_sys->providers_list.end())
    {
        msg_Err(p_sd, "Unknown provider '%s'", provider);
        return NULL;
    }

    char *up;
    if (asprintf(&up, "%s@%s", user, provider) < 0)
        return NULL;
    if (p_sys->providers_items.count(up))
    {
        free(up);
        return NULL;
    }
    char *uri;
    asprintf(&uri, "vlc-cloud://%s/", up);
    auto *item = input_item_NewExt(uri, uri, INPUT_DURATION_INDEFINITE,
                                   ITEM_TYPE_NODE, ITEM_NET);
    if (item) {
        input_item_SetMeta(item, vlc_meta_Title, up);
        input_item_AddOption(item, "no-auto-preparse", VLC_INPUT_OPTION_TRUSTED);
    }
    free(uri);
    free(up);
    return item;
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

static int CallbackAuthentication(
    vlc_object_t *, char const *, vlc_value_t, vlc_value_t newval, void *data)
{
    auto *p_sd = (services_discovery_t*)data;
    auto *p_sys = (services_discovery_sys_t*)p_sd->p_sys;
    if (strcmp(newval.psz_string, "ABORT") != 0)
    {
        services_discovery_AddItem(p_sd, p_sys->auth_item->item);
        p_sys->providers_items
            [p_sys->auth_item->item->psz_name] = p_sys->auth_item;
        p_sys->auth_item = nullptr;
    }
    p_sys->auth_progress = false;
    return VLC_SUCCESS;
}

static int CallbackRequestedFromUI(
    vlc_object_t *, char const *, vlc_value_t, vlc_value_t newval, void *data)
{
    auto *p_sd = (services_discovery_t*)data;
    auto *p_sys = (services_discovery_sys_t*)p_sd->p_sys;
    std::string cmd(newval.psz_string);
    auto sep = cmd.find(':');
    auto op = cmd.substr(0, sep);
    auto arg = cmd.substr(sep+1);

    if (op == "ADD")
    {
        if (p_sys->auth_progress)
            return VLC_EGENERIC;
        if (p_sys->auth_item) { delete p_sys->auth_item; p_sys->auth_item = nullptr; }
        const char *user=nullptr, *prov=nullptr;
        auto pos = arg.find('@');
        if (pos==std::string::npos)
        {
            user = GenerateUserIdentifier(p_sd, arg.c_str());
            prov = arg.c_str();
        }
        else
        {
            user = arg.c_str();
            prov = arg.c_str()+pos+1;
        }
        auto *item = GetNewUserInput(p_sd, user, prov);
        p_sys->auth_item = new provider_item_t(item, nullptr);
        p_sys->auth_progress = true;
    }
    else if (op == "RM")
    {
        services_discovery_RemoveItem(
            p_sd,
            ((services_discovery_sys_t*)p_sd->p_sys)
              ->providers_items[arg]->item);
        delete ((services_discovery_sys_t*)p_sd->p_sys)
               ->providers_items[arg];
        ((services_discovery_sys_t*)p_sd->p_sys)
            ->providers_items.erase(arg);
    }
    else if (op == "ACT")
    {
        auto it = p_sys->providers_items.find(arg);
        if (it != p_sys->providers_items.end() && it->second && it->second->item) {
            vlc_thread_t th;
            if (vlc_clone(&th, SD_WorkerThread, new std::pair<services_discovery_t*, input_item_t*>(p_sd, it->second->item)) == 0)
                p_sys->workers.push_back(th);
        }
    }
    return VLC_SUCCESS;
}

provider_item_t::provider_item_t(input_item_t *item, input_thread_t *)
    : item(item), thread(nullptr) {}

provider_item_t::~provider_item_t()
{
    input_item_Release(item);
}

static char *GenerateUserIdentifier(
    services_discovery_t *p_sd, const char *provider)
{
    vlc_url_t dummy;
    vlc_UrlParse(&dummy, (std::string("cloudstorage://")+provider).c_str());
    vlc_credential cred;
    vlc_credential_init(&cred, &dummy);
    vlc_credential_get(&cred, p_sd, NULL, NULL, NULL, NULL);
    unsigned id=1;
    char *gen;
    while(true)
    {
        asprintf(&gen, "user%u", id++);
        bool exists=false;
        for (unsigned i=0;i<cred.i_entries_count;i++)
            if (!strcmp(cred.p_entries[i].ppsz_values[KEY_USER], gen))
            { free(gen); exists=true; break; }
        if (!exists) break;
    }
    vlc_credential_clean(&cred);
    vlc_UrlClean(&dummy);
    return gen;
}
