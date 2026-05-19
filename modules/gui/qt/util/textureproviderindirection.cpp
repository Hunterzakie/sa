/*****************************************************************************
 * Copyright (C) 2024 VLC authors and VideoLAN
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
#include "textureproviderindirection.hpp"

#include <QSGTextureProvider>
#include <QRunnable>
#include <QMutexLocker>
#include <QJSEngine>

// For RHI sanity check:
#if __has_include(<QtGui/rhi/qrhi.h>)
// RHI is semi-public since Qt 6.6, but still requires gui-private.
#define RHI_PUBLIC
#define RHI_AVAILABLE
#include <QtGui/rhi/qrhi.h>
#elif __has_include(<QtGui/private/qrhi_p.h>) && __has_include(<QtQuick/private/qquickwindow_p.h>)
#warning "It is recommended to use Qt 6.6 or greater."
#define RHI_AVAILABLE
#include <QtGui/private/qrhi_p.h>
#include <QtQuick/private/qquickwindow_p.h>
#endif

#include "util/rhireadbacktexturejob.hpp"

class TextureProviderCleaner : public QRunnable
{
public:
    explicit TextureProviderCleaner(QSGTextureProvider *textureProvider)
        : m_textureProvider(textureProvider) { }

    void run() override
    {
        delete m_textureProvider;
    }

private:
    const QPointer<QSGTextureProvider> m_textureProvider;
};

TextureProviderIndirection::TextureProviderIndirection(QQuickItem *parent)
    : QQuickItem(parent)
{

}

TextureProviderIndirection::~TextureProviderIndirection()
{
    {
        if (m_textureProvider)
        {
            // https://doc.qt.io/qt-6/qquickitem.html#graphics-resource-handling

            // `QQuickItem::releaseResources()` is called before the item is disassociated from its window:
            // "This happens when the item is about to be removed from the window it was previously rendering to."
            // Therefore, we can not have a texture provider during destruction, but not the window:
            assert(window());
            window()->scheduleRenderJob(new TextureProviderCleaner(m_textureProvider), QQuickWindow::BeforeSynchronizingStage);
            m_textureProvider = nullptr;
        }
    }
}

bool TextureProviderIndirection::isTextureProvider() const
{
    return true;
}

QSGTextureProvider *TextureProviderIndirection::textureProvider() const
{
    // This method is called from the rendering thread.

    if (!m_textureProvider)
    {
        m_textureProvider = new QSGTextureViewProvider;

        const auto adjustSource = [provider = m_textureProvider](const QQuickItem *source) {
            if (source)
            {
                assert(source->isTextureProvider() &&
                       "TextureProviderIndirection: " \
                       "TextureProviderIndirection's source item is not a texture provider. " \
                       "Layering can be enabled for the source item in order to make " \
                       "it a texture provider.");

                provider->setTextureProvider(source->textureProvider());
            }
            else
            {
                provider->setTextureProvider(nullptr);
            }
        };

        const auto synchronizeState = [weakThis = QPointer(this), provider = m_textureProvider]() {
            if (Q_UNLIKELY(!weakThis))
                return;

            provider->setFiltering(weakThis->m_filtering);
            provider->setMipmapFiltering(weakThis->m_mipmapFiltering);
            provider->setAnisotropyLevel(weakThis->m_anisotropyLevel);
            provider->setHorizontalWrapMode(weakThis->m_horizontalWrapMode);
            provider->setVerticalWrapMode(weakThis->m_verticalWrapMode);

            if (weakThis->m_detachAtlasTextures)
                provider->requestDetachFromAtlas();
        };

        // These are going to be queued when necessary:
        connect(this, &TextureProviderIndirection::sourceChanged, m_textureProvider, adjustSource);
        connect(this, &TextureProviderIndirection::rectChanged, m_textureProvider, &QSGTextureViewProvider::setRect, Qt::DirectConnection);

        connect(this, &TextureProviderIndirection::filteringChanged, m_textureProvider, &QSGTextureViewProvider::setFiltering);
        connect(this, &TextureProviderIndirection::mipmapFilteringChanged, m_textureProvider, &QSGTextureViewProvider::setMipmapFiltering);
        connect(this, &TextureProviderIndirection::anisotropyLevelChanged, m_textureProvider, &QSGTextureViewProvider::setAnisotropyLevel);
        connect(this, &TextureProviderIndirection::horizontalWrapModeChanged, m_textureProvider, &QSGTextureViewProvider::setHorizontalWrapMode);
        connect(this, &TextureProviderIndirection::verticalWrapModeChanged, m_textureProvider, &QSGTextureViewProvider::setVerticalWrapMode);

        connect(this, &TextureProviderIndirection::detachAtlasTexturesChanged, m_textureProvider, [provider = m_textureProvider](bool detach) {
            if (detach)
                provider->requestDetachFromAtlas();
        });

        // When the target texture changes, the texture view may reset its state, so we need to synchronize in that case:
        connect(m_textureProvider, &QSGTextureProvider::textureChanged, m_textureProvider, synchronizeState); // Executed in texture provider's thread

        // Initial adjustments:
        adjustSource(m_source);
        if (m_rect.isValid())
            m_textureProvider->setRect(m_rect);
        synchronizeState();
    }
    return m_textureProvider;
}

void TextureProviderIndirection::resetTextureSubRect()
{
    m_rect = {};
    emit rectChanged({});
}

bool TextureProviderIndirection::updateTexture()
{
    return updateTexture(nullptr, std::function<void()>());
}

bool TextureProviderIndirection::updateTexture(QObject *context, std::function<void()> callback)
{
    if (!m_source)
        return false;

    const auto w = window();
    if (!w)
    {
        qCritical() << "TextureProviderIndirection::updateTexture(): window is not available!";
        return false;
    }

    // Note that `beforeSynchronizing()` is signalled when the GUI thread is blocked,
    // this means we are already in synchronization phase. We don't want to use
    // `afterSynchronizing()`, because doing so would make `textureToImage()` to
    // wait advance one frame, which would be unnecessary waiting.
    connect(w, &QQuickWindow::beforeSynchronizing, this, [weakThis = QPointer(this), callback, context = QPointer(context)]() {
        if (!weakThis)
        {
            qCritical() << "TextureProviderIndirection::updateTexture(): texture provider indirection is no longer available!";
            return;
        }

        // This initializes the texture provider, if it does not already exist:
        const auto tp = weakThis->textureProvider();

        if (!tp)
        {
            qCritical() << "TextureProviderIndirection::updateTexture(): failed to initialize or get the texture provider!";
            return;
        }

        const auto texture = qobject_cast<QSGDynamicTexture*>(tp->texture());

        if (!texture)
        {
            qCritical() << "TextureProviderIndirection::updateTexture(): failed to get `QSGDynamicTexture`!";
            return;
        }

        // As the docs note, this should be called during synchronization:
        texture->updateTexture();

        if (callback)
        {
            if (context)
            {
                if (context->thread() == QThread::currentThread())
                    callback();
                else
                    QMetaObject::invokeMethod(context, [callback]() { callback(); }, Qt::QueuedConnection);
            }
            else
            {
                callback();
            }
        }
    }, static_cast<Qt::ConnectionType>(Qt::DirectConnection | Qt::SingleShotConnection));

    return true;
}

bool TextureProviderIndirection::updateTexture(QObject *context, QJSValue callback)
{
    assert(callback.isCallable());
    return updateTexture(context, [callback, weakContext = QPointer(context)] () mutable {
        if (Q_LIKELY(weakContext))
        {
            // There is no TOCTOU condition risk here, the callback is called when the
            // gui thread is blocked.
            const auto engine = qjsEngine(weakContext);

            assert(engine);
            assert(engine->thread() == weakContext->thread());
            if (QThread::currentThread() != engine->thread())
            {
                // std::move is to make sure that we do not hold reference to callback
                // in a (scene graph) thread, which is not the engine's thread:
                QMetaObject::invokeMethod(engine, [callback = std::move(callback)]() {
                    assert(callback.isCallable());
                    std::move(callback).call();
                }, Qt::QueuedConnection);
            }
            else
            {
                assert(callback.isCallable());
                std::move(callback).call();
            }
        }
        else
        {
            // We do not need to assert, callback is not that important here (unlike ::textureToImage())
            qWarning() << "TextureProviderIndirection::updateTexture(): context is no longer available, can not call the callback!";
            // Callback may be destroyed in a thread different than the engine's thread, but there is not much we can do here.
        }
    });
}

bool TextureProviderIndirection::textureToImage(QObject *context, const QJSValue& callback)
{
    assert(callback.isCallable());
    return textureToImageImpl(context, callback);
}

bool TextureProviderIndirection::textureToImage(QObject *context,
                                                std::function<void (const QImage &)> callback)
{
    return textureToImageImpl(context, callback);
}

void TextureProviderIndirection::invalidateSceneGraph()
{
    // https://doc.qt.io/qt-6/qquickitem.html#graphics-resource-handling

    // This slot is called from the rendering thread.
    {
        if (m_textureProvider)
        {
            delete m_textureProvider;
        }
    }
}

void TextureProviderIndirection::releaseResources()
{
    // https://doc.qt.io/qt-6/qquickitem.html#graphics-resource-handling

    // This method is called from the GUI thread.

    // QQuickItem::releaseResources() is guaranteed to have a valid window when it is called:
    assert(window());
    {
        if (m_textureProvider)
        {
            window()->scheduleRenderJob(new TextureProviderCleaner(m_textureProvider), QQuickWindow::BeforeSynchronizingStage);
            m_textureProvider = nullptr;
        }
    }

    QQuickItem::releaseResources();
}

bool TextureProviderIndirection::rhiSanityCheck()
{
#ifdef RHI_AVAILABLE
    // Sanity check:
    const auto w = window();

    if (!w)
        return false;

#ifdef RHI_PUBLIC
    QRhi* const rhi = w->rhi();
#else
    const QQuickWindowPrivate *const privateWindow = QQuickWindowPrivate::get(w);
    assert(privateWindow);
    QRhi* const rhi = privateWindow->rhi;
#endif

    if (!rhi)
        return false;

#ifdef RHI_PUBLIC
    QRhiSwapChain* const swapChain = w->swapChain();
#else
    QRhiSwapChain* const swapChain = privateWindow->swapchain;
#endif

    if (!swapChain)
        return false;

    return true;
#else
    return false;
#endif
}

void QSGTextureViewProvider::adjustTexture()
{
    if (m_textureProvider)
        m_textureView.setTexture(m_textureProvider->texture());
    else
        m_textureView.setTexture(nullptr);

    // `textureChanged()` is emitted implicitly, no need to emit here explicitly again.
}

QSGTextureViewProvider::QSGTextureViewProvider()
    : QSGTextureProvider()
{
    connect(&m_textureView, &QSGTextureView::updateRequested, this, &QSGTextureProvider::textureChanged);
}

QSGTexture *QSGTextureViewProvider::texture() const
{
    if (m_textureProvider && m_textureView.texture())
        return &m_textureView;
    else
        return nullptr;
}

void QSGTextureViewProvider::setTextureProvider(const QSGTextureProvider *textureProvider)
{
    if (m_textureProvider == textureProvider)
        return;

    if (m_textureProvider)
        disconnect(m_textureProvider, &QSGTextureProvider::textureChanged, this, &QSGTextureViewProvider::adjustTexture);

    m_textureProvider = textureProvider;

    if (m_textureProvider)
    {
        connect(m_textureProvider, &QSGTextureProvider::textureChanged, this, &QSGTextureViewProvider::adjustTexture);
        connect(m_textureProvider, &QObject::destroyed, this, [this]() { setTextureProvider(nullptr); });
    }

    adjustTexture();
}

void QSGTextureViewProvider::setRect(const QRect &rect)
{
    m_textureView.setRect(rect);
    // `textureChanged()` is emitted implicitly, no need to emit here explicitly again
}

void QSGTextureViewProvider::setMipmapFiltering(QSGTexture::Filtering filter)
{
    if (m_textureView.mipmapFiltering() == filter)
        return;

    if (filter != QSGTexture::Filtering::None)
    {
        const auto targetTexture = m_textureView.texture();
        // If there is no target texture, we can accept mipmap filtering. When there becomes a target texture, `QSGTextureView` should
        // consider this itself anyway if the new target texture has no mipmaps. Workarounds should probably not be over-conservative,
        // we should not dismiss the case if there is no target texture now but the upcoming texture has mip maps.
        if (targetTexture && !targetTexture->hasMipmaps())
        {
            // Having mip map filtering when there are no mip maps may be problematic with certain graphics backends (like OpenGL).
            return;
        }
    }

    m_textureView.setMipmapFiltering(filter);
    emit textureChanged();
}

void QSGTextureViewProvider::setFiltering(QSGTexture::Filtering filter)
{
    if (m_textureView.filtering() == filter)
        return;

    m_textureView.setFiltering(filter);
    emit textureChanged();
}

void QSGTextureViewProvider::setAnisotropyLevel(QSGTexture::AnisotropyLevel level)
{
    if (m_textureView.anisotropyLevel() == level)
        return;

    m_textureView.setAnisotropyLevel(level);
    emit textureChanged();
}

void QSGTextureViewProvider::setHorizontalWrapMode(QSGTexture::WrapMode hwrap)
{
    if (m_textureView.horizontalWrapMode() == hwrap)
        return;

    m_textureView.setHorizontalWrapMode(hwrap);
    emit textureChanged();
}

void QSGTextureViewProvider::setVerticalWrapMode(QSGTexture::WrapMode vwrap)
{
    if (m_textureView.verticalWrapMode() == vwrap)
        return;

    m_textureView.setVerticalWrapMode(vwrap);
    emit textureChanged();
}

void QSGTextureViewProvider::requestDetachFromAtlas()
{
    m_textureView.requestDetachFromAtlas();
}

template<typename T>
bool TextureProviderIndirection::textureToImageImpl(QObject *context, const T& callback)
{
    bool sanityCheck = false;
    QPointer<QQuickWindow> weakQuickWindow;

    if (QThread::currentThread() == thread())
    {
        sanityCheck = rhiSanityCheck();
        weakQuickWindow = window();
    }
    else
    {
        QMetaObject::invokeMethod(this, [this, &sanityCheck, &weakQuickWindow]() {
            sanityCheck = rhiSanityCheck();
            weakQuickWindow = window();
        }, Qt::BlockingQueuedConnection);
    }

    if (!sanityCheck)
    {
        qDebug() << "TextureProviderIndirection::textureToImage(): rhi is not available.";
        return false;
    }

    assert(weakQuickWindow); // `rhiSanityCheck()` would have caught it, this is not aggressive assertion.

    // We have to create this here, and not in the callback, because it needs to be created
    // in js engine's thread (when the callback is `QJSValue` type).
    const auto imageRenderJob = new RhiReadBackTextureJob(weakQuickWindow, this, context, callback);

    // Context is not provided so that callback is not queued. It is assumed that `QQuickWindow::scheduleRenderJob()`
    // can be called from the rendering thread, at least during synchronization phase (GUI thread is blocked).
    const bool ret = updateTexture(nullptr, [weakQuickWindow, imageRenderJob]() {        
        // There is no TOCTOU here, because (as noted) this callback is only called during synchronization,
        // when GUI thread is blocked.
        if (Q_UNLIKELY(!weakQuickWindow))
        {
            qCritical() << "TextureProviderIndirection::textureToImage(): window is no longer available!";
            return;
        }

        // This will be the same frame because this stage comes after during synchronization, this is good,
        // because it means less waiting (no need to wait frame advance). Also note that the window takes
        // the ownership:
        weakQuickWindow->scheduleRenderJob(imageRenderJob, QQuickWindow::AfterSynchronizingStage);
    });

    if (Q_UNLIKELY(!ret))
        delete imageRenderJob;

    return ret;
}
