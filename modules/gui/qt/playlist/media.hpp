/*****************************************************************************
 * Copyright (C) 2019 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef VLC_QT_MEDIA_HPP_
#define VLC_QT_MEDIA_HPP_

#include <QString>
#include <QStringList>
#include <QByteArray>

#include "util/qt_dirs.hpp"
#include "util/shared_input_item.hpp"

// `lib/libvlc_internal.h`
extern "C" VLC_WEAK void input_item_SetLibvlcOwner(input_item_t *, void *, void (*release)(void *));

namespace vlc {
  namespace playlist {

class Media
{
    static void ReleaseBuffer(void *buffer)
    {
        delete static_cast<QByteArray*>(buffer);
    }

    static int imem_get_cb(void *opaque, const char *cookie,
                           int64_t *dts, int64_t *pts, unsigned *flags,
                           size_t *size, void **data)
    {
        Q_UNUSED(cookie);
        Q_UNUSED(dts);
        Q_UNUSED(pts);
        Q_UNUSED(flags);

        assert(opaque);
        assert(data);
        assert(size);

        const auto buffer = static_cast<QByteArray*>(opaque);
        if (Q_UNLIKELY(buffer->size() <= 0 || !buffer->constData()))
            return 1;

        *size = buffer->size();
        *data = buffer->data();

        return 0;
    }

    static void imem_release_cb(void *opaque, const char *cookie, size_t, void *)
    {
        Q_UNUSED(opaque);
        Q_UNUSED(cookie);

        // We don't free the buffer with `imem-release`, the buffer
        // is to be released when the input item itself is gone.
    }

public:
    Media(input_item_t *media = nullptr, const QStringList &options = {})
    {
        if (media)
        {
            /* the media must be unique in the playlist */
            ptr.reset(input_item_Copy(media), false);
            if (!ptr)
                throw std::bad_alloc();
            setMediaOptions(options);
        }
    }

    Media(const QString &uri, const QString &name, const QStringList &options = {})
    {
        auto uUri = uri.toUtf8();
        auto uName = name.toUtf8();
        const char *rawUri = uUri.isNull() ? nullptr : uUri.constData();
        const char *rawName = uName.isNull() ? nullptr : uName.constData();
        ptr.reset(input_item_New(rawUri, rawName), false);
        if (!ptr)
            throw std::bad_alloc();
        setMediaOptions(options);
    }

    explicit Media(const QByteArray& data)
    {
        if (Q_UNLIKELY(!input_item_SetLibvlcOwner))
        {
            throw std::runtime_error( "Weak symbol input_item_SetLibvlcOwner() could not be "
                                      "resolved. Direct data item can not be played!" );
        }

        assert(data.size() > 0);

        const auto inputItem = input_item_New("imem://", nullptr);
        if (Q_UNLIKELY(!inputItem))
            throw std::bad_alloc();

        ptr.reset(inputItem, false);

        const auto buffer = new QByteArray(data);
        input_item_SetLibvlcOwner(inputItem, buffer, ReleaseBuffer);

        // Akin to `libvlc_media_new_callbacks()`:
        setMediaOptions({QString::asprintf(":imem-get=%lld", reinterpret_cast<long long>(imem_get_cb)),
                         QString::asprintf(":imem-release=%lld", reinterpret_cast<long long>(imem_release_cb)),
                         QString::asprintf(":imem-data=%lld", reinterpret_cast<long long>(buffer)),
                         QString::asprintf(":imem-size=%lld", reinterpret_cast<long long>(buffer->size())),
                         QString::asprintf(":imem-cat=4")});
    }

    void setMediaOptions(const QStringList &options = {})
    {
        if (options.count() > 0)
        {
            char **ppsz_options = NULL;
            int i_options = 0;

            ppsz_options = new char *[options.count()];
            auto optionDeleter = vlc::wrap_carray<char*>(ppsz_options, [&i_options](char *ptr[]) {
                for(int i = 0; i < i_options; i++)
                    free(ptr[i]);
                delete[] ptr;
            });

            for (int i = 0; i < options.count(); i++)
            {
                QString option = colon_unescape( options[i] );
                ppsz_options[i] = strdup(option.toUtf8().constData());
                if (!ppsz_options[i])
                    throw std::bad_alloc();
                i_options++;
            }
            input_item_AddOptions( ptr.get(), i_options, ppsz_options, VLC_INPUT_OPTION_TRUSTED );
        }
    }

    operator bool() const
    {
        return static_cast<bool>(ptr);
    }

    input_item_t *raw() const
    {
        return ptr.get();
    }

private:
    SharedInputItem ptr;
};

  } // namespace playlist
} // namespace vlc

#endif
