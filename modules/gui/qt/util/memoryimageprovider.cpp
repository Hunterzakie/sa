/*****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
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
#include "memoryimageprovider.hpp"


BasicMemoryImageProvider::BasicMemoryImageProvider(QQmlImageProviderBase::Flags flags)
    : QQuickImageProvider(QQmlImageProviderBase::Image, flags)
    , IMemoryImageProvider()
{

}

bool BasicMemoryImageProvider::removeImage(const QString &id)
{
    QWriteLocker locker(&m_readWriteLock);
    return m_storage.remove(id);
}

QString BasicMemoryImageProvider::registerImage(const QImage &image, bool persistent, const QString &id)
{
    if (image.isNull())
    {
        qCritical() << "BasicImageProvider::registerImage(): image is null!";
        return {};
    }

    QString effectiveId;

    if (!id.isEmpty())
    {
        effectiveId = id;
    }
    else
    {
        assert(internalCounter < SIZE_MAX);

        effectiveId = QStringLiteral("_internal#") + QString::number(internalCounter++);
        assert(!m_storage.contains(effectiveId));
    }

    {
        QWriteLocker locker(&m_readWriteLock);
        m_storage[effectiveId] = QPair<QImage, bool>{image, persistent};
    }

    // qDebug() << "BasicImageProvider: registered" << effectiveId
    //          << "with image:" << image;

    return effectiveId;
}

QImage BasicMemoryImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // QImage is implicitly shared
    QPair<QImage, bool> data;
    {
        QReadLocker locker(&m_readWriteLock);
        data = m_storage[id];
    }

    QImage image = data.first;
    if (image.isNull())
    {
        qWarning() << "BasicImageProvider: image not found with id:" << id;
        return {};
    }

    assert(size);
    const auto imageSize = image.size();
    *size = imageSize;

    if (!requestedSize.isEmpty() && requestedSize != imageSize)
    {
        image = image.scaled(requestedSize);
    }

    if (!data.second)
    {
        QMetaObject::invokeMethod(this, [this, id]() {
            QWriteLocker locker(&m_readWriteLock);
            m_storage.remove(id);
        }, Qt::QueuedConnection);
    }

    // qDebug() << "BasicImageProvider: requested" << id
    //          << "with size" << requestedSize
    //          << "providing image:" << image;

    return image;
}

