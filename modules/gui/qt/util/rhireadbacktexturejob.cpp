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
#include "rhireadbacktexturejob.hpp"

#include <QQuickItem>
#include <QSGTextureProvider>
#include <QSGTexture>
#include <QQmlEngine>
#include <QJSEngine>

#if __has_include(<QtGui/rhi/qrhi.h>)
// RHI is semi-public since Qt 6.6, but still requires gui-private.
#define RHI_AVAILABLE
#include <QtGui/rhi/qrhi.h>
#elif __has_include(<QtGui/private/qrhi_p.h>) && __has_include(<QtQuick/private/qquickwindow_p.h>)
#warning "It is recommended to use Qt 6.6 or greater."
#define RHI_AVAILABLE
#include <QtGui/private/qrhi_p.h>
#include <QtQuick/private/qquickwindow_p.h>
#else
#warning "ImageRenderJob requires rhi headers, it won't be functional!"
#endif

#include "util/memoryimageprovider.hpp" // IMemoryImageProvider

#ifdef RHI_AVAILABLE
static void ImageCleanupHandler(void *data)
{
    // Deleting void pointer is undefined. We either do
    // this, or use `malloc()/free()` instead:
    const auto result = static_cast<QRhiReadbackResult *>(data);
    // qDebug() << "Deleting QRhiReadbackResult" << result;
    delete result;
}
#endif

template<typename CallbackType>
RhiReadBackTextureJob<CallbackType>::RhiReadBackTextureJob(QPointer<QQuickWindow> window,
                                                           QPointer<QQuickItem> textureProviderItem,
                                                           QPointer<QObject> context,
                                                           CallbackType callback)
    : m_window(window)
    , m_context(context)
    , m_callback(callback)
    , m_textureProviderItem(textureProviderItem)
{
#ifndef RHI_AVAILABLE
    qCritical() << "ImageRenderJob: rhi is not available, it won't be functional! Re-build the application with rhi headers and try again.";
#endif

    assert(window);
    assert(textureProviderItem);

    // There must be a callback, otherwise the job would be useless:
    if constexpr (std::is_same<CallbackType, QJSValue>::value)
    {
        assert(callback.isCallable());

        if (!m_context)
        {
            m_context = static_cast<QObject*>(m_textureProviderItem.get());
        }

        assert(m_context);
        assert(QThread::currentThread() == m_context->thread());
        assert(qjsEngine(m_context));
        assert(QThread::currentThread() == qjsEngine(m_context)->thread());

        // We need to make sure that the `QJSValue` callback is destroyed in
        // engine/context's thread. We could use unique pointer, but if
        // the operation fails, it may cause the object to be deleted in
        // a different thread. Doing this prevents it, and is also simpler.
        // Note that this won't cause any leaks if the operation fails,
        // because the context would delete the property when it dies,
        // which in turn decrease the reference count for `QJSValue`.
        const auto jobIdStr = jobId().toLatin1();
        assert(m_context->property(jobIdStr.constData()).isNull());
        m_context->setProperty(jobIdStr.constData(), callback.toVariant(QJSValue::RetainJSObjects));
    }
    else
    {
        assert(callback);
    }
}

template<typename CallbackType>
void RhiReadBackTextureJob<CallbackType>::run()
{
#ifdef RHI_AVAILABLE
    // Scene graph thread, but during synchronization so GUI thread is blocked.
    // This means that there is no TOCTOU condition risk here.
    // TODO: Add assertion regarding the demanded scene graph phase.

    if (Q_UNLIKELY(!m_window))
    {
        qCritical() << "ImageRenderJob: window is no longer valid!";
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    QRhi* const rhi = m_window->rhi();
#else
    const QQuickWindowPrivate *const privateWindow = QQuickWindowPrivate::get(m_window);
    QRhi* const rhi = privateWindow->rhi;
#endif

    if (!rhi)
    {
        qCritical() << "ImageRenderJob:" << m_window << "does not have valid QRhi instance!";
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    QRhiSwapChain* const swapChain = m_window->swapChain();
#else
    QRhiSwapChain* const swapChain = privateWindow->swapchain;
#endif

    if (!swapChain)
    {
        qCritical() << "ImageRenderJob:" << m_window << "does not have valid QRhiSwapChain instance!";
        return;
    }

    if (Q_UNLIKELY(!m_textureProviderItem))
    {
        qCritical() << "ImageRenderJob: texture provider item is no longer available!";
        return;
    }

    assert(m_textureProviderItem->isTextureProvider());

    // This initializes the texture provider, if it does not already exist:
    const auto textureProvider = m_textureProviderItem->textureProvider();
    assert(textureProvider);

    QSGTexture *const qsgTexture = textureProvider->texture();
    if (!qsgTexture)
    {
        qCritical() << "ImageRenderJob: texture provider does not provide a texture!";
        return;
    }

    QRhiTexture *const rhiTexture = qsgTexture->rhiTexture();
    if (!rhiTexture)
    {
        qCritical() << "ImageRenderJob: texture provider does not provide a rhi texture!";
        return;
    }

    // https://doc.qt.io/qt-6/qrhiresourceupdatebatch.html#readBackTexture

    QRhiReadbackResult *rbResult = new QRhiReadbackResult;

    rbResult->completed = [rbResult,
                           callback = m_callback,
                           context = m_context,
                           dpr = m_window->effectiveDevicePixelRatio(),
                           jobIdStr = jobId().toLatin1()]() {
        {
            // Be careful here, there is no guarantee that that the GUI thread is blocked.
            const QImage::Format fmt = QImage::Format_RGBA8888_Premultiplied;

            const uchar *p = reinterpret_cast<const uchar *>(rbResult->data.constData());

            // QImage does not manage the data here, so provide the delete function which
            // would delete `rbResult` that owns the data when its reference count drops to 0.
            // Deletion of `QRhiReadbackResult` will likely happen in a different thread, but
            // that should assumed to be okay (creation happens in scene graph thread). If
            // that is a problem, we can schedule a cleanup in scene graph in the deletion
            // function, but for now it is not deemed to be necessary.
            QImage image(p,
                         rbResult->pixelSize.width(),
                         rbResult->pixelSize.height(),
                         fmt,
                         ImageCleanupHandler,
                         rbResult);
            image.setDevicePixelRatio(dpr);

            if constexpr (std::is_same<CallbackType, QJSValue>::value)
            {
                const auto callCallback = [callback,
                                           image,
                                           context,
                                           jobIdStr]() {
                    if (Q_UNLIKELY(!context))
                    {
                        qCritical() << "ImageRenderJob: context is gone!";
                        return;
                    }

                    assert(QThread::currentThread() == context->thread());

                    QQmlEngine * const engine = qmlEngine(context);

                    if (Q_UNLIKELY(!engine))
                    {
                        qCritical() << "ImageRenderJob: could not fetch the qml engine!";
                        return;
                    }

                    assert(QThread::currentThread() == engine->thread());

                    QString imageProviderId;
                    {
                        const auto memoryImageProviderName = QStringLiteral("memory");

                        const auto memoryImageProvider = dynamic_cast<IMemoryImageProvider*>(engine->imageProvider(memoryImageProviderName));
                        if (memoryImageProvider)
                        {
                            const auto id = memoryImageProvider->registerImage(image, false);
                            if (Q_LIKELY(!id.isEmpty()))
                            {
                                imageProviderId = QStringLiteral("image://%1/%2").arg(memoryImageProviderName, id);
                            }
                            else
                            {
                                qWarning() << "ImageRenderJob: IMemoryImageProvider::registerImage() failed with image" << image;
                            }
                        }
                        else
                        {
                            qWarning() << "ImageRenderJob: qml engine"
                                       << engine
                                       << "does not have IMemoryImageProvider image provider registered as 'memory'!";
                        }
                    }

                    // Anonymous gadget:
                    const auto result = ImageContainer(image, imageProviderId);
                    const auto jsValueResult = engine->toScriptValue(result);
                    assert(!jsValueResult.isUndefined());

                    assert(callback.isCallable());

                    callback.call({jsValueResult});

                    // To make sure callback is deleted in engine's thread:
                    // This is delayed, because this callback still holds the `QJSValue` that it captured.
                    // An alternative could be using a mutable lambda and moving the `QJSValue`, but I got
                    // compilation error with `QMetaObject::invokeMethod()`.
                    QMetaObject::invokeMethod(engine, [engine, context, callback, jobIdStr]() {
                        // No need to assert context here, if it is (for any reason)
                        // destroyed, we still hold a reference to `QJSValue` and it
                        // can be destroyed here (in engine's thread).
                        if (Q_LIKELY(context))
                        {
                            assert(context->thread() == engine->thread()); // Most likely useless
                            assert(!context->property(jobIdStr.constData()).isNull());
                            context->setProperty(jobIdStr.constData(), {});
                        }
                    }, Qt::QueuedConnection);
                };

                assert(qApp);
                if (qApp->thread() != QThread::currentThread())
                {
                    // This is done to prevent TOCTOU:
                    QMetaObject::invokeMethod(qApp, [context, callCallback]() {
                        assert(context);
                        if (Q_UNLIKELY(context->thread() != QThread::currentThread()))
                        {
                            // Unlikely, context most likely lives in the GUI thread, but still:
                            QMetaObject::invokeMethod(context, callCallback, Qt::QueuedConnection);
                        }
                        else
                        {
                            callCallback();
                        }
                    }, Qt::QueuedConnection);
                }
                else
                {
                    assert(context);
                    QMetaObject::invokeMethod(context, callCallback, Qt::QueuedConnection);
                }
            }
            else
            {
                const auto callCallback = [callback, image]() {
                    assert(callback);
                    callback(image);
                };

                assert(qApp);
                if (qApp->thread() != QThread::currentThread())
                {
                    // This is done to prevent TOCTOU:
                    QMetaObject::invokeMethod(qApp, [context, callCallback]() {
                        if (Q_UNLIKELY(context && context->thread() != QThread::currentThread()))
                        {
                            // Unlikely, context most likely lives in the GUI thread, but still:
                            QMetaObject::invokeMethod(context, callCallback, Qt::QueuedConnection);
                        }
                        else
                        {
                            // No context provided
                            callCallback();
                        }
                    }, Qt::QueuedConnection);
                }
                else if (context && context->thread() == QThread::currentThread())
                {
                    assert(context);
                    QMetaObject::invokeMethod(context, callCallback, Qt::QueuedConnection);
                }
                else
                {
                    // No context provided, and thread is GUI thread:
                    callCallback();
                }
            }
        }
    };

    QRhiCommandBuffer *cb = swapChain->currentFrameCommandBuffer();
    QRhiResourceUpdateBatch *resourceUpdates = rhi->nextResourceUpdateBatch();

    QRhiReadbackDescription readbackDesc(rhiTexture);
    resourceUpdates->readBackTexture(readbackDesc, rbResult);

    cb->resourceUpdate(resourceUpdates);
#endif
}

template<typename CallbackType>
QString RhiReadBackTextureJob<CallbackType>::jobId() const
{
    if (!m_jobId)
        m_jobId = QString::number(reinterpret_cast<qintptr>(this));
    return *m_jobId;
}

template class RhiReadBackTextureJob<QJSValue>;
template class RhiReadBackTextureJob<std::function<void(const QImage&)>>;
