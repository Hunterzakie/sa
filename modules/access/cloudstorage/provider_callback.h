/*****************************************************************************
 * provider_callback.h: Inherit class ICallback from libcloudstorage
 *****************************************************************************
 * Copyright (C) 2025 VideoLabs and VideoLAN
 *
 * Authors: Diogo Silva <dbtdsilva@gmail.com>
 *          William Ung <williamung@msn.com>
 *          Maksym Yemelianenko <max.yemelianenko@gmail.com>
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
#ifndef VLC_CLOUDSTORAGE_PROVIDER_CALLBACK_H
#define VLC_CLOUDSTORAGE_PROVIDER_CALLBACK_H

#include "access.h"

#include <vlc_dialog.h>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <sstream>

class Callback final : public cloudstorage::ICloudProvider::IAuthCallback {
public:
    explicit Callback(stream_t* access)
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
            var_SetString(vlc_object_instance(p_access), "cloudstorage-auth", "ABORT");
        } else {
            msg_Info(p_access, "Authentication successful for %s!", provider.name().c_str());
            p_sys->authenticated = true;
            p_sys->token = provider.token();
            msg_Dbg(p_access, "Token length: %zu", p_sys->token.length());
            var_SetString(vlc_object_instance(p_access), "cloudstorage-auth", "SUCCESS");
        }
    }

private:
    void openInBrowser(const std::string& url) const {
        static constexpr std::string_view opener =
#ifdef __APPLE__
            "open";
#elif defined(__linux__)
            "xdg-open";
#elif defined(_WIN32)
            "start";
#else
            "";
#endif
        if (opener.empty()) {
            msg_Warn(p_access, "Cannot open browser automatically, please visit: %s", url.c_str());
            return;
        }
        std::string cmd = std::string(opener) + ' ' + url;
        if (std::system(cmd.c_str()) != 0) {
            msg_Warn(p_access, "Failed to launch browser, open manually: %s", url.c_str());
        } else {
            msg_Info(p_access, "Opened browser for URL: %s", url.c_str());
        }
    }

    stream_t* p_access;
    access_sys_t* p_sys;
};

#endif // VLC_CLOUDSTORAGE_PROVIDER_CALLBACK_H
