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
#ifndef MEMORYIMAGEPROVIDER_HPP
#define MEMORYIMAGEPROVIDER_HPP

#include <QQuickImageProvider>
#include <QUrl>
#include <QImage>
#include <QDir>
#include <QReadWriteLock>
// #include <QtQml/qqmlregistration.h> // QML_ANONYMOUS

class IMemoryImageProvider
{
public:
    // Registers an image, and returns the effective id.
    // If `persistent` is true, the image will be shared owned by the provider forever.
    // If `persistent` is false, the image will be shared owned by the provider only until
    // it is requested with `requestImage()` with at least one event loop cycle grace period.
    // This is a good option for transient usages, like using an image as drag image source.
    // This method must be thread-safe:
    virtual QString registerImage(const QImage& image, bool persistent = true, const QString& id = {}) = 0;

    // This method must be thread-safe:
    virtual bool removeImage(const QString& id) = 0;
};

class BasicMemoryImageProvider : public QQuickImageProvider, public IMemoryImageProvider
{
    Q_OBJECT

    // NOTE: QImage is implicitly shared.
    QHash<QString, QPair<QImage, bool>> m_storage;
    QReadWriteLock m_readWriteLock; // Protects m_storage and m_pendingRemoval

    size_t internalCounter = 0;

public:
    explicit BasicMemoryImageProvider(QQmlImageProviderBase::Flags flags = {});

    bool removeImage(const QString& id) override;
    QString registerImage(const QImage& image, bool persistent = true, const QString& id = {}) override;
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

// Similar to `QQuickItemGrabResult`, but is a gadget.
class ImageContainer
{
    Q_GADGET

    Q_PROPERTY(QImage image READ image CONSTANT)
    Q_PROPERTY(QUrl url READ url CONSTANT)

    // QML_ANONYMOUS

public:
    explicit ImageContainer(QImage image, QUrl url)
        : m_image(image)
        , m_url(url)
    { }

    QImage image() const
    {
        return m_image;
    }

    QUrl url(bool saveToFileIfNecessary = true) const
    {
        if (Q_LIKELY(m_url.isValid()))
        {
            return m_url;
        }
        else if (saveToFileIfNecessary)
        {
            qWarning() << "ImageRenderResult: url not provided, trying saving image to a temporary file and returning its url instead!";
            const QString path = QDir::tempPath() + '/' + QStringLiteral("vlc-qt-imagerenderresult-%1.png").arg(QString::number(reinterpret_cast<qintptr>(this))) ;
            if (saveToFile(path))
            {
                m_url = path;
                return path;
            }
            else
            {
                qCritical() << "ImageRenderResult::url(): failed to save image to file.";
            }
        }

        return m_url;
    }

    Q_INVOKABLE bool saveToFile(const QUrl &fileName) const
    {
        assert(fileName.isLocalFile());
        return m_image.save(fileName.toLocalFile());
    }

private:
    const QImage m_image; // QImage is implicitly shared
    mutable QUrl m_url;
};

#endif // MEMORYIMAGEPROVIDER_HPP
