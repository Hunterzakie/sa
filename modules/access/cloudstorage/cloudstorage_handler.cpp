/*****************************************************************************
 * cloudstorage_handler.cpp: Implementation for libcloudstorage
 *****************************************************************************
 * Copyright (C) 2025 VideoLabs and VideoLAN
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
 *****************************************************************************/
#include "cloudstorage_handler.h"
#include "token_cache.h"

#include <vlc_common.h>
#include <vlc_dialog.h>
#include <vlc_input.h>
#include <vlc_keystore.h>
#include <vlc_spawn.h>
#include <vlc_stream.h>
#include <vlc_url.h>

#include <cloudstorage/ICloudStorage.h>
#include <cloudstorage/IHttpServer.h>

#include <cerrno>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <string>

#include "cloudstorage_common.h"
#include "vlc_http.h"
#include "vlc_httpd_server.h"
#include "media_filter.h"

using cloudstorage::ICloudProvider;
using cloudstorage::ICloudStorage;
using cloudstorage::IItem;


class NoPromptCallback final
    : public cloudstorage::ICloudProvider::IAuthCallback {
public:
  explicit NoPromptCallback(stream_t *) {}
  Status userConsentRequired(const cloudstorage::ICloudProvider &) override {
    return Status::None;
  }
  void done(const cloudstorage::ICloudProvider &,
            cloudstorage::EitherError<void>) override {}
};

class AuthRouteCallback final
    : public cloudstorage::ICloudProvider::IAuthCallback {
public:
  explicit AuthRouteCallback(stream_t *access,
                             std::shared_ptr<ScopedSemaphore> sem,
                             bool *auth_triggered)
      : p_access(access),
        p_sys(reinterpret_cast<access_sys_t *>(access->p_sys)), m_sem(sem),
        m_auth_triggered(auth_triggered), m_dialog(nullptr) {}

  ~AuthRouteCallback() override {
    if (m_dialog != nullptr) {
      vlc_dialog_release(p_access, m_dialog);
    }
  }

  Status
  userConsentRequired(const cloudstorage::ICloudProvider &provider) override {
    vlc_mutex_lock(&p_sys->lock);
    bool cancelled = p_sys->auth_cancelled;
    vlc_mutex_unlock(&p_sys->lock);
    if (cancelled) {
      return Status::None;
    }

    if (m_auth_triggered) {
      *m_auth_triggered = true;
    }
    std::string url = provider.authorizeLibraryUrl();
    msg_Info(p_access,
             "=== Authentication Required for %s ===", provider.name().c_str());
    msg_Info(p_access, "AUTHORIZATION URL: %s", url.c_str());

    msg_Info(p_access, "Opening browser for authentication...");
    openInBrowser(url);

    m_dialog = vlc_dialog_display_progress(
        p_access, true, 0.0f, "Cancel",
        "Cloud Storage Authentication",
        "Please complete authentication for %s in your browser:\n%s",
        provider.name().c_str(), url.c_str());
    vlc_mutex_lock(&p_sys->lock);
    p_sys->auth_dialog = m_dialog;
    vlc_mutex_unlock(&p_sys->lock);

    msg_Info(p_access, "Waiting for authorization code...");
    return Status::WaitForAuthorizationCode;
  }

  void done(const cloudstorage::ICloudProvider &provider,
            cloudstorage::EitherError<void> error) override {
    if (m_dialog != nullptr) {
      vlc_mutex_lock(&p_sys->lock);
      p_sys->auth_dialog = nullptr;
      vlc_mutex_unlock(&p_sys->lock);
      vlc_dialog_release(p_access, m_dialog);
      m_dialog = nullptr;
    }

    if (error.left()) {
      msg_Err(p_access, "Authorization Error %d: %s", error.left()->code_,
              error.left()->description_.c_str());
      p_sys->authenticated = false;
    } else {
      msg_Info(p_access, "Authentication successful for %s!",
               provider.name().c_str());
      p_sys->authenticated = true;
      p_sys->token = provider.token();
      msg_Info(p_access, "Token length: %zu", p_sys->token.length());
    }
    m_sem->post();
  }

private:
  void openInBrowser(const std::string &url) const {
#if defined(__APPLE__)
#if TARGET_OS_IPHONE || TARGET_OS_TV
    (void)url;
#else
    const char *args[] = {"open", url.c_str(), nullptr};
    pid_t pid;
    const int fdv[] = {-1, -1, -1, -1};
    if (vlc_spawnp(&pid, "open", fdv, args) != 0) {
      std::string cmd = "open \"" + url + "\" &";
      std::system(cmd.c_str());
    }
#endif
#elif defined(__linux__)
    const char *args[] = {"xdg-open", url.c_str(), nullptr};
    pid_t pid;
    const int fdv[] = {-1, -1, -1, -1};
    if (vlc_spawnp(&pid, "xdg-open", fdv, args) != 0) {
      std::string cmd = "xdg-open \"" + url + "\" &";
      std::system(cmd.c_str());
    }
#elif defined(_WIN32)
    std::string cmd = "start \"\" \"" + url + "\"";
    std::system(cmd.c_str());
#endif
  }

  stream_t *p_access;
  access_sys_t *p_sys;
  std::shared_ptr<ScopedSemaphore> m_sem;
  bool *m_auth_triggered;
  vlc_dialog_id *m_dialog;
};

static ssize_t CloudRead(stream_t *p_access, void *buf, size_t len);
static int CloudControl(stream_t *p_access, int i_query, va_list args);
static int CloudSeek(stream_t *p_access, uint64_t i_pos);

class CloudReadCallback : public cloudstorage::IDownloadFileCallback {
public:
  explicit CloudReadCallback(stream_t *access) : p_access(access) {}

  void receivedData(const char *data, uint32_t length) override {
    auto p_sys = reinterpret_cast<access_sys_t *>(p_access->p_sys);
    if (!p_sys) return;
    vlc_mutex_lock(&p_sys->lock);

    while (p_sys->total_queued_bytes > 5 * 1024 * 1024 && !p_sys->aborted) {
      vlc_cond_wait(&p_sys->wait, &p_sys->lock);
    }

    if (p_sys->aborted) {
      vlc_mutex_unlock(&p_sys->lock);
      return;
    }

    p_sys->chunks.emplace_back(data, length);
    p_sys->total_queued_bytes += length;
    vlc_cond_signal(&p_sys->wait);
    vlc_mutex_unlock(&p_sys->lock);
  }

  void progress(uint64_t, uint64_t) override {}

  void done(cloudstorage::EitherError<void> err) override {
    auto p_sys = reinterpret_cast<access_sys_t *>(p_access->p_sys);
    if (!p_sys) return;
    vlc_mutex_lock(&p_sys->lock);
    if (p_sys->aborted) {
      p_sys->req_finished = true;
      vlc_cond_signal(&p_sys->wait);
      vlc_mutex_unlock(&p_sys->lock);
      return;
    }
    p_sys->req_finished = true;
    if (err.left()) {
      if (err.left()->code_ == 416) {
        p_sys->req_error = false;
      } else {
        msg_Err(p_access, "Async stream error: %s",
                err.left()->description_.c_str());
        p_sys->req_error = true;
      }
    }
    vlc_cond_signal(&p_sys->wait);
    vlc_mutex_unlock(&p_sys->lock);
  }

private:
  stream_t *p_access;
};

static int GetCredentials(stream_t *p_access);
static int InitProvider(stream_t *p_access);
static int AddItem(input_item_node_t *p_node, stream_t *p_access,
                   IItem::Pointer item);

template <typename F, typename Obj, typename... Args>
static auto WrapAsync(stream_t *p_access, F f, Obj &&obj, Args &&...args) {
  auto p_sys = static_cast<access_sys_t *>(p_access->p_sys);
  ScopedSemaphore sem;
  auto finish = [&sem](auto) { sem.post(); };
  decltype((std::forward<Obj>(obj).*f)(std::forward<Args>(args)...,
                                       finish)) req;
  try {
    req = (std::forward<Obj>(obj).*f)(std::forward<Args>(args)..., finish);
  } catch (const std::exception &e) {
    msg_Err(p_access, "Exception in auth: %s", e.what());
    msg_Err(p_access, "Cloud request threw before start");
    return decltype(req->result().right())();
  }
  if (!req) {
    msg_Err(p_access, "Cloud request failed to start (null request)");
    return decltype(req->result().right())();
  }

  while (vlc_sem_timedwait(&sem.sem, vlc_tick_now() + VLC_TICK_FROM_MS(250)) ==
         ETIMEDOUT) {
    vlc_mutex_lock(&p_sys->lock);
    vlc_dialog_id *dialog = p_sys->auth_dialog;
    bool cancelled = dialog && vlc_dialog_is_cancelled(p_access, dialog);
    if (cancelled)
      p_sys->auth_cancelled = true;
    vlc_mutex_unlock(&p_sys->lock);
    if (cancelled) {
      msg_Info(p_access, "Authentication cancelled by user");
      req->cancel();
      sem.wait();
      break;
    }
  }

  decltype(req->result()) outcome;
  try {
    outcome = req->result();
  } catch (const std::exception &e) {
    msg_Err(p_access, "Exception in auth: %s", e.what());
    msg_Err(p_access, "Cloud request result threw");
    return decltype(outcome.right())();
  }

  if (outcome.left()) {
    msg_Err(p_access, "Cloud request failed (%d): %s", outcome.left()->code_,
            outcome.left()->description_.c_str());
    if (outcome.left()->code_ == 401 || outcome.left()->code_ == 403) {
      msg_Info(p_access, "Authentication failed, clearing stored token");
      p_sys->token.clear();
      p_sys->authenticated = false;
      TokenCache::clear(p_access, &p_sys->url);
    }
    return decltype(outcome.right())();
  }

  if (!p_sys->token.empty()) {
    p_sys->authenticated = true;
    /* Only persist the token when operating on the actual provider URL.
     * During the auth flow, psz_host is "auth" and saving here would create
     * a stale keystore entry (server="auth") alongside the correct one
     * (server=provider_name) saved after the token exchange. */
    std::string host = p_sys->url.psz_host ? p_sys->url.psz_host : "";
    if (host != "auth")
      TokenCache::set(p_access, &p_sys->url, p_sys->token);
  }
  return outcome.right();
}

int HandleCloudProviderRequest(stream_t *p_access) {
  msg_Info(p_access, "HandleCloudProviderRequest: url=%s",
           p_access->psz_url ? p_access->psz_url : "(null)");
  {
    auto p_sys = reinterpret_cast<access_sys_t *>(p_access->p_sys);
    std::string host = p_sys->url.psz_host ? p_sys->url.psz_host : "";
    if (host == "auth") {
      const char *path = p_sys->url.psz_path ? p_sys->url.psz_path : "/";
      std::string path_str = path;
      if (!path_str.empty() && path_str.front() == '/')
        path_str.erase(0, 1);
      std::string provider_name;
      size_t slash = path_str.find('/');
      provider_name =
          (slash == std::string::npos) ? path_str : path_str.substr(0, slash);
      if (provider_name.empty()) {
        return VLC_EGENERIC;
      }

      auto sem = std::make_shared<ScopedSemaphore>();
      bool auth_triggered = false;

      ICloudProvider::InitData init;
      init.token_ = std::string();
      init.permission_ = ICloudProvider::Permission::ReadWrite;
      init.callback_ =
          std::make_shared<AuthRouteCallback>(p_access, sem, &auth_triggered);
      init.http_engine_ =
          std::make_unique<vlc::cloudstorage::VLCHttp>(VLC_OBJECT(p_access));
      init.http_server_ =
          std::make_unique<vlc::cloudstorage::VLCHttpServerFactory>(
              VLC_OBJECT(p_access));
      init.hints_ = p_sys->hints;
      msg_Info(p_access, "Auth init: client_id=[%s] secret_len=%zu",
               p_sys->hints.count("client_id")
                   ? p_sys->hints.at("client_id").c_str()
                   : "(empty)",
               p_sys->hints.count("client_secret")
                   ? p_sys->hints.at("client_secret").size()
                   : 0);
      cloudstorage::ICloudProvider::Pointer prov;
      try {
        prov = ICloudStorage::create()->provider(provider_name.c_str(),
                                                 std::move(init));
      } catch (const std::exception &e) {
        msg_Err(p_access, "Exception in auth: %s", e.what());
      }
      if (!prov) {
        return VLC_EGENERIC;
      }
      cloudstorage::IItem::Pointer root = prov->rootDirectory();
      if (!root)
        root = WrapAsync(p_access, &ICloudProvider::getItemAsync, *prov, "/");
      if (root)
        (void)WrapAsync(p_access, &ICloudProvider::listDirectorySimpleAsync,
                        *prov, root);

      if (auth_triggered) {
        msg_Info(p_access, "Waiting for token exchange to complete...");
        sem->wait();
        msg_Info(p_access, "Token exchange wait finished!");

        vlc_mutex_lock(&p_sys->lock);
        bool cancelled = p_sys->auth_cancelled;
        vlc_mutex_unlock(&p_sys->lock);
        if (cancelled) {
          msg_Info(p_access, "Authentication cancelled by user, aborting");
          return VLC_EGENERIC;
        }

        if (root)
          (void)WrapAsync(p_access, &ICloudProvider::listDirectorySimpleAsync,
                          *prov, root);
      }

      std::string token = prov->token();
      if (!token.empty()) {
        vlc_url_t key = p_sys->url;
        key.psz_host = const_cast<char *>(provider_name.c_str());
        if (!key.psz_username || !*key.psz_username)
          key.psz_username = const_cast<char *>("default");
        TokenCache::set(p_access, &key, token);
      }

      std::string redirect_back = "cloudstorage://";
      if (p_sys->url.psz_username && *p_sys->url.psz_username) {
        redirect_back += p_sys->url.psz_username;
        redirect_back += "@";
      }
      redirect_back += provider_name;
      redirect_back += "/";

      msg_Info(p_access, "Authentication complete. Redirecting back to %s",
               redirect_back.c_str());
      p_access->psz_url = strdup(redirect_back.c_str());
      return VLC_ACCESS_REDIRECT;
    }
  }
  int err;
  if ((err = GetCredentials(p_access)) != VLC_SUCCESS)
    return err;
  if ((err = InitProvider(p_access)) != VLC_SUCCESS)
    return err;

  auto p_sys = reinterpret_cast<access_sys_t *>(p_access->p_sys);

  if (p_sys->current_item &&
      p_sys->current_item->type() == IItem::FileType::Directory) {
    const bool had_token = !p_sys->token.empty();
    auto probe = WrapAsync(p_access, &ICloudProvider::listDirectorySimpleAsync,
                           *p_sys->provider, p_sys->current_item);
    if (!probe) {
      if (had_token) {
        std::string auth_mrl = "cloudstorage://";
        if (p_sys->url.psz_username && *p_sys->url.psz_username) {
          auth_mrl += p_sys->url.psz_username;
          auth_mrl += "@";
        }
        auth_mrl += "auth/";
        if (p_sys->url.psz_host && *p_sys->url.psz_host)
          auth_mrl += p_sys->url.psz_host;
        auth_mrl += "/";
        msg_Info(
            p_access,
            "Token invalid for %s; redirecting to %s for re-authentication",
            p_sys->url.psz_host ? p_sys->url.psz_host : "(null)",
            auth_mrl.c_str());
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

  p_sys->i_pos = 0;
  p_access->pf_read = CloudRead;
  p_access->pf_control = CloudControl;
  p_access->pf_seek = CloudSeek;
  return VLC_SUCCESS;
}

static int GetCredentials(stream_t *p_access) {
  auto p_sys = reinterpret_cast<access_sys_t *>(p_access->p_sys);
  const char *user =
      p_sys->url.psz_username ? p_sys->url.psz_username : "(null)";
  const char *host = p_sys->url.psz_host ? p_sys->url.psz_host : "(null)";
  msg_Info(p_access, "Looking for creds %s@%s", user, host);

  std::string tok = TokenCache::get(p_access, &p_sys->url);
  if (!tok.empty()) {
    p_sys->token = tok;
    p_sys->authenticated = false;
    return VLC_SUCCESS;
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
  msg_Info(p_access, "No token for %s; redirecting to %s for authentication",
           host, auth_mrl.c_str());
  p_access->psz_url = strdup(auth_mrl.c_str());
  if (!p_access->psz_url)
    return VLC_ENOMEM;
  return VLC_ACCESS_REDIRECT;
}

static int InitProvider(stream_t *p_access) {
  auto p_sys = reinterpret_cast<access_sys_t *>(p_access->p_sys);
  if (!p_sys->url.psz_host) {
    msg_Err(p_access, "No provider host in URL");
    return VLC_EGENERIC;
  }
  ICloudProvider::InitData init;
  init.token_ = p_sys->token;
  init.permission_ = ICloudProvider::Permission::ReadWrite;
  init.callback_ = std::make_shared<NoPromptCallback>(p_access);
  init.hints_ = p_sys->hints;
  init.http_engine_ =
      std::make_unique<vlc::cloudstorage::VLCHttp>(VLC_OBJECT(p_access));
  init.http_server_ = std::make_unique<vlc::cloudstorage::VLCHttpServerFactory>(
      VLC_OBJECT(p_access));

  try {
    p_sys->provider =
        ICloudStorage::create()->provider(p_sys->url.psz_host, std::move(init));
  } catch (const std::exception &e) {
    msg_Err(p_access, "Exception in auth: %s", e.what());
    p_sys->provider.reset();
  }
  if (!p_sys->provider) {
    msg_Err(p_access, "Failed to create provider for host %s",
            p_sys->url.psz_host);
    return VLC_EGENERIC;
  }

  try {
    if (std::string(p_sys->url.psz_path ? p_sys->url.psz_path : "/") == "/") {
      if (auto root = p_sys->provider->rootDirectory())
        p_sys->current_item = root;
      else
        p_sys->current_item = WrapAsync(p_access, &ICloudProvider::getItemAsync,
                                        *p_sys->provider, "/");
    } else {
      const char *dec = vlc_uri_decode(p_sys->url.psz_path);
      p_sys->current_item =
          WrapAsync(p_access, &ICloudProvider::getItemAsync, *p_sys->provider,
                    dec ? dec : p_sys->url.psz_path);
    }
  } catch (const std::exception &e) {
    msg_Err(p_access, "Exception in auth: %s", e.what());
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

int ReadDir(stream_t *p_access, input_item_node_t *p_node) {
  auto p_sys = reinterpret_cast<access_sys_t *>(p_access->p_sys);
  if (!p_sys->provider || !p_sys->current_item) {
    msg_Err(p_access, "ReadDir without initialized provider/current_item");
    return VLC_EGENERIC;
  }
  auto list = WrapAsync(p_access, &ICloudProvider::listDirectorySimpleAsync,
                        *p_sys->provider, p_sys->current_item);
  if (!list)
    return VLC_EGENERIC;
  for (auto &itm : *list) {
    if (!vlc::cloudstorage::IsSupportedMediaFile(itm)) {
      msg_Dbg(p_access, "Skipping non-media item: %s", itm->filename().c_str());
      continue;
    }
    if (AddItem(p_node, p_access, itm) != VLC_SUCCESS)
      return VLC_EGENERIC;
  }
  return VLC_SUCCESS;
}

static int AddItem(input_item_node_t *p_node, stream_t *p_access,
                   IItem::Pointer item) {
  std::string url = p_access->psz_url ? p_access->psz_url : "";
  if (url.empty())
    url = "cloudstorage://";
  if (url.back() != '/')
    url.push_back('/');

  char *enc = vlc_uri_encode(item->filename().c_str());
  if (!enc)
    return VLC_ENOMEM;
  url += enc;
  free(enc);

  msg_Info(p_access, "CloudStorage: Found item: %s (%s)",
           item->filename().c_str(),
           (item->type() == IItem::FileType::Directory) ? "Directory" : "File");

  input_item_t *i =
      (item->type() == IItem::FileType::Directory)
          ? input_item_NewDirectory(url.c_str(), item->filename().c_str(),
                                    ITEM_NET)
          : input_item_NewFile(url.c_str(), item->filename().c_str(),
                               INPUT_DURATION_UNSET, ITEM_NET);
  if (!i)
    return VLC_EGENERIC;
  auto nod = input_item_node_AppendItem(p_node, i);
  input_item_Release(i);
  return nod ? VLC_SUCCESS : VLC_EGENERIC;
}

static int CloudControl(stream_t *p_access, int i_query, va_list args) {
  msg_Info(p_access, "Entering CloudControl, query: %d", i_query);
  auto p_sys = reinterpret_cast<access_sys_t *>(p_access->p_sys);
  if (!p_sys || !p_sys->current_item)
    return VLC_EGENERIC;

  switch (i_query) {
  case STREAM_CAN_SEEK:
    *va_arg(args, bool *) = true;
    return VLC_SUCCESS;
  case STREAM_GET_SIZE: {
    uint64_t size = p_sys->current_item->size();
    if (size != cloudstorage::IItem::UnknownSize) {
      *va_arg(args, uint64_t *) = size;
      return VLC_SUCCESS;
    }
    return VLC_EGENERIC;
  }
  case STREAM_CAN_PAUSE:
  case STREAM_CAN_CONTROL_PACE:
    *va_arg(args, bool *) = true;
    return VLC_SUCCESS;
  case STREAM_CAN_FASTSEEK:
    *va_arg(args, bool *) = false;
    return VLC_SUCCESS;
  case STREAM_GET_PTS_DELAY:
    *va_arg(args, vlc_tick_t *) =
        VLC_TICK_FROM_MS(var_InheritInteger(p_access, "network-caching"));
    return VLC_SUCCESS;
  default:
    return VLC_EGENERIC;
  }
}

static int CloudSeek(stream_t *p_access, uint64_t i_pos) {
  msg_Info(p_access, "Entering CloudSeek to pos: %" PRIu64, i_pos);
  auto p_sys = reinterpret_cast<access_sys_t *>(p_access->p_sys);

  vlc_mutex_lock(&p_sys->lock);
  p_sys->aborted = true;
  vlc_cond_broadcast(&p_sys->wait);
  vlc_mutex_unlock(&p_sys->lock);

  if (p_sys->current_req) {
    p_sys->current_req->cancel();
  }

  vlc_mutex_lock(&p_sys->lock);
  while (!p_sys->req_finished) {
    vlc_cond_wait(&p_sys->wait, &p_sys->lock);
  }
  vlc_mutex_unlock(&p_sys->lock);

  p_sys->current_req.reset();

  vlc_mutex_lock(&p_sys->lock);
  p_sys->aborted = false;
  p_sys->chunks.clear();
  p_sys->total_queued_bytes = 0;
  p_sys->current_chunk_offset = 0;
  p_sys->req_finished = true;
  p_sys->req_error = false;
  p_sys->i_pos = i_pos;
  vlc_mutex_unlock(&p_sys->lock);

  return VLC_SUCCESS;
}

static ssize_t CloudRead(stream_t *p_access, void *buf, size_t len) {
  auto p_sys = reinterpret_cast<access_sys_t *>(p_access->p_sys);

  if (p_sys->current_item &&
      p_sys->current_item->size() != cloudstorage::IItem::UnknownSize &&
      p_sys->i_pos >= p_sys->current_item->size()) {
    return 0; // Fast-path EOF
  }

  vlc_mutex_lock(&p_sys->lock);

  if (p_sys->req_finished && !p_sys->req_error && p_sys->chunks.empty()) {
    p_sys->req_finished = false;
    p_sys->req_error = false;
    cloudstorage::Range range{p_sys->i_pos, cloudstorage::Range::Full};
    auto item = p_sys->current_item;
    auto *provider = p_sys->provider.get();
    vlc_mutex_unlock(&p_sys->lock);

    if (!provider) {
      vlc_mutex_lock(&p_sys->lock);
      p_sys->req_error = true;
      p_sys->req_finished = true;
      vlc_mutex_unlock(&p_sys->lock);
      return -1;
    }

    auto cb = std::make_shared<CloudReadCallback>(p_access);
    auto req = provider->downloadFileAsync(item, std::move(cb), range);

    vlc_mutex_lock(&p_sys->lock);
    p_sys->current_req = std::move(req);
    if (!p_sys->current_req) {
      p_sys->req_error = true;
      p_sys->req_finished = true;
    }
  }

  size_t copied = 0;
  while (copied < len && (!p_sys->chunks.empty() || !p_sys->req_finished)) {
    if (p_sys->chunks.empty()) {
      if (p_sys->req_error) {
        break;
      }
      if (p_sys->req_finished) {
        break;
      }
      if (copied > 0) {
        // Return partial read immediately to prevent blocking VLC
        break;
      }
      vlc_cond_wait(&p_sys->wait, &p_sys->lock);
      continue;
    }
    std::string &chunk = p_sys->chunks.front();
    size_t avail = chunk.size() - p_sys->current_chunk_offset;
    size_t to_copy = std::min(len - copied, avail);

    memcpy(static_cast<char *>(buf) + copied,
           chunk.data() + p_sys->current_chunk_offset, to_copy);
    copied += to_copy;
    p_sys->current_chunk_offset += to_copy;

    if (p_sys->current_chunk_offset >= chunk.size()) {
      p_sys->total_queued_bytes -= chunk.size();
      p_sys->chunks.pop_front();
      p_sys->current_chunk_offset = 0;
      vlc_cond_signal(&p_sys->wait);
    }
  }

  p_sys->i_pos += copied;
  vlc_mutex_unlock(&p_sys->lock);

  if (copied == 0 && p_sys->req_error)
    return -1;
  return copied;
}
