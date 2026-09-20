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
#include "qsgcomputenode.hpp"

#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0) // RHI is semi-public since Qt 6.6
#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#elif __has_include(<QtGui/private/qrhi_p.h>) && __has_include(<QtQuick/private/qquickwindow_p.h>)
#include <QtQuick/private/qquickwindow_p.h>
#include <QtGui/private/qrhi_p.h>
#include <QtGui/private/qshader_p.h>
#else
#error "QSGComputeNode requires Qt declarative private headers if Qt version is lower than 6.6!"
#endif

#include <QQuickWindow>
#include <QRunnable>
#include <QFile>
#include <QMutexLocker>
#include <QThread>
#include <QCache>
#include <QEventLoop>
#include <QTimer>

// You can schedule a compute render job using this class, with your
// compute pipeline (and resource update batch, if wanted).

// The current command buffer of the frame can be gotten through
// `QQuickWindow::swapChain()`, and
// `QRhiSwapChain::currentFrameCommandBuffer()`. So, you don't (and
// most likely should not) need to create an offscreen frame just for
// this purpose. The graphics backend will execute the compute shader
// in the same frame where the rendering happens, and this operation
// may time-wise be even free, if you have a powerful GPU that is
// already throttled by v-sync.

// This job should be executed when the current buffer is not already
// recording commands (not inside a pass), which means, in QSG rendering
// context, before or after the renderer starts rendering the Qt Quick
// items with a non-compute pass. This means that you can use
// `QQuickWindow::scheduleRenderJob()` with render stages that are
// outside the non-compute/graphical pass. Also note that, if it is not
// already obvious, this job must be executed in rendering/RHI thread,
// and the parameters provided must outlive the job instance (but not
// necessarily until doneCallback is called, unless Qt RHI itself
//  mandates that).

// This is currently private, but it may be public in the near future.
class ComputeRenderJob : public QRunnable
{
    QRhiCommandBuffer *const m_commandBuffer;
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
    QRhi *const m_rhi;
#endif
    QRhiComputePipeline *const m_computePipeline;
    QRhiShaderResourceBindings *const m_computeSrb;
    const std::tuple<int, int, int> m_dispatchParameters;
    QRhiBuffer *const m_outputBuffer;
    QRhiTexture *const m_outputTexture;
    const std::function<void(const QVariant&)> m_doneCallback;
    QRhiResourceUpdateBatch *const m_resourceUpdateBatch;

    static void ImageCleanupHandler(void *data)
    {
        delete static_cast<QByteArray*>(data);
    }

public:
    explicit ComputeRenderJob(QRhiCommandBuffer *cb,
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
                              QRhi *rhi,
#endif
                              QRhiComputePipeline *pipeline,
                              QRhiShaderResourceBindings *srb,
                              const std::tuple<int, int, int>& dispatchParameters,
                              QRhiBuffer *outputBuffer, // You need to provide either an output buffer ...
                              QRhiTexture *outputTexture, // ... or output texture
                              // This function is called in render/RHI thread, and the GUI
                              // thread may not be blocked. Use synchronization:
                              std::function<void(const QVariant&)> doneCallback,
                              QRhiResourceUpdateBatch *ub = nullptr)
        : m_commandBuffer(cb)
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
        , m_rhi(rhi)
#endif
        , m_computePipeline(pipeline)
        , m_computeSrb(srb)
        , m_dispatchParameters(dispatchParameters)
        , m_outputBuffer(outputBuffer)
        , m_outputTexture(outputTexture)
        , m_doneCallback(doneCallback)
        , m_resourceUpdateBatch(ub) // Not necessary (the input buffer may have been updated externally)
    {
        assert(cb);
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
        assert(rhi);
#endif
        assert(pipeline);
        assert(srb);
        assert(outputBuffer || outputTexture);
        assert(doneCallback);
    }

    void run() override
    {
        m_commandBuffer->beginComputePass(m_resourceUpdateBatch);

        m_commandBuffer->setComputePipeline(m_computePipeline);
        m_commandBuffer->setShaderResources(m_computeSrb);

        // TODO: Don't manually unpack the tuple:
        m_commandBuffer->dispatch(std::get<0>(m_dispatchParameters),
                                  std::get<1>(m_dispatchParameters),
                                  std::get<2>(m_dispatchParameters));

        // https://doc.qt.io/qt-6/qrhiresourceupdatebatch.html#readBackTexture
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
        QRhiBufferReadbackResult *bufferReadbackResult = nullptr;
        QRhiReadbackResult *textureReadbackResult = nullptr;
        if (m_outputBuffer)
            bufferReadbackResult = new QRhiBufferReadbackResult;
        else if (m_outputTexture)
            textureReadbackResult = new QRhiReadbackResult;
        QRhi *const rhi = m_rhi;
#else
        const auto readbackResult = new QRhiReadbackResult;
        QRhi *const rhi = m_commandBuffer->rhi();
#endif
        QRhiResourceUpdateBatch *readbackBatch = rhi->nextResourceUpdateBatch();
        if (m_outputBuffer)
        {
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
            const auto readbackResult = bufferReadbackResult;
#endif
            const auto cb = [readbackResult, cb = m_doneCallback]() {
                cb(QVariant::fromValue(readbackResult->data));
                delete readbackResult;
            };
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
            bufferReadbackResult->completed = cb;
#else
            readbackResult->completed = cb;
#endif
            readbackBatch->readBackBuffer(m_outputBuffer, 0, m_outputBuffer->size(), readbackResult);
        }
        else if (m_outputTexture)
        {
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
            const auto readbackResult = textureReadbackResult;
#endif
            const auto cb = [readbackResult, cb = m_doneCallback]() {
                // FIXME: We need to care about other formats too, but it needs mapping which is tedious:
                assert(readbackResult->format == QRhiTexture::RGBA8);

                const auto imageData = new QByteArray(readbackResult->data);

                const QImage::Format fmt = QImage::Format_ARGB32_Premultiplied;
                const uchar *p = reinterpret_cast<const uchar *>(imageData->constData());
                QImage image(p,
                             readbackResult->pixelSize.width(),
                             readbackResult->pixelSize.height(),
                             fmt,
                             ImageCleanupHandler, // A lesson from the lottie module (!8318).
                             imageData);

                cb(QVariant::fromValue(image));

                delete readbackResult;
            };
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
            textureReadbackResult->completed = cb;
#else
            readbackResult->completed = cb;
#endif

            QRhiReadbackDescription desc;
            desc.setTexture(m_outputTexture);
            readbackBatch->readBackTexture(desc, readbackResult);
        }

        m_commandBuffer->endComputePass(readbackBatch);
    }
};

static QPair<QRhi*, QRhiSwapChain*> getRhiAndSwapChainFromWindow(const QQuickWindow* window)
{
    assert(window);
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
    const QQuickWindowPrivate *const privateWindow = QQuickWindowPrivate::get(window);
    const auto rhi = privateWindow->rhi;
    const auto swapChain = privateWindow->swapchain;
#else
    const auto rhi = window->rhi();
    const auto swapChain = window->swapChain();
#endif
    return {rhi, swapChain};
}

QSGComputeNode::QSGComputeNode(const QString &computeShader, QQuickWindow *window)
    : m_window(window)
{
    if (!computeShader.isEmpty())
    {
        setShader(computeShader);
    }
}

QSGComputeNode::QSGComputeNode(QShader *computeShader, const QShaderDescription *computeShaderDescription, QQuickWindow *window)
    : m_window(window)
{
    if (computeShader && computeShaderDescription)
        setShader(computeShader, computeShaderDescription);
}

QSGComputeNode::~QSGComputeNode()
{
    setShader({}); // This will delete the RHI objects appropriately.
}

bool QSGComputeNode::supported(const QQuickWindow *window)
{
    assert(window);

    const auto [rhi, swapChain] = getRhiAndSwapChainFromWindow(window);

    if (rhi && swapChain)
        return rhi->isFeatureSupported(QRhi::Compute);
    else
        return false; // Non-rhi (software or openvg), or compute is not supported
}

bool QSGComputeNode::computeAsync(const std::tuple<int, int, int> &dispatchParameters, std::function<void (const QVariant &)> doneCallback)
{
    assert(m_window);

    const auto [rhi, swapChain] = getRhiAndSwapChainFromWindow(m_window);

    assert(rhi);

    const auto currentThread = QThread::currentThread();
    QRunnable *renderJob = nullptr;

    if (!isPrepared())
    {
        if (currentThread == rhi->thread())
        {
            // Shortcut:
            if (!prepare())
                return false;
        }
        else
        {
            // Since `prepare()` must be called in RHI thread, we do this:
            class PrepareAndComputeRenderJob : public QRunnable
            {
                QSGComputeNode *const m_node;
                const std::tuple<int, int, int> m_dispatchParameters;
                const std::function<void (const QVariant &)> m_doneCallback;

            public:
                explicit PrepareAndComputeRenderJob(QSGComputeNode *node,
                                                    const std::tuple<int, int, int> &dispatchParameters,
                                                    std::function<void (const QVariant &)> doneCallback)
                    : m_node(node)
                    , m_dispatchParameters(dispatchParameters)
                    , m_doneCallback(doneCallback)
                {
                    assert(node);
                }

                void run() override
                {
                    if (m_node->prepare())
                    {
                        const auto renderJob = m_node->newComputeRenderJob(m_dispatchParameters, m_doneCallback);
                        if (renderJob)
                        {
                            QMutexLocker lock(&m_node->m_lock); // May not be really necessary, but nevertheless...
                            // Schedule the compute job in after the Qt's graphical pass in this case so that we don't
                            // wait for the next frame:
                            m_node->m_window->scheduleRenderJob(renderJob, QQuickWindow::RenderStage::AfterRenderingStage);
                        }
                    }
                }
            };

            renderJob = new PrepareAndComputeRenderJob(this, dispatchParameters, doneCallback);
        }
    }

    if (!renderJob)
        renderJob = newComputeRenderJob(dispatchParameters, doneCallback);

    if (renderJob)
    {
        QMutexLocker locker(&m_lock);

        m_window->scheduleRenderJob(renderJob, QQuickWindow::RenderStage::BeforeRenderingStage); // Scheduled before Qt Quick's own graphical pipeline/pass

        if (currentThread == m_window->thread())
        {
            m_window->update(); // Request a new frame, in case it is not implicitly scheduled due to dirty Qt Quick items for the rendering pipeline.
        }
        else
        {
            // "Updates can only be scheduled from GUI thread or from QQuickItem::updatePaintNode()"
            QMetaObject::invokeMethod(m_window, &QQuickWindow::update, Qt::QueuedConnection);
        }

        return true;
    }
    else
    {
        return false;
    }
}

bool QSGComputeNode::computeAsync(const QString &computeShader,
                                  const QVariant &inputData,
                                  QQuickWindow *window,
                                  const std::tuple<int, int, int> &dispatchParameters,
                                  std::function<void (const QVariant &)> doneCallback)
{
    assert(window);

    QSGComputeNode *node = nullptr;
    bool ownedByCache = false;

    // TODO: Investigate if it is worth having a pool of cached nodes, currently it is
    //       not really cared because the most time taking part (reading and deserializing
    //       the shader) is already permanently cached in `deserializedShaderHash`.
    static QMutex cacheLock;
    static QCache<QString, QSGComputeNode> computeNodeCache;
    static QHash<QString, QPair<QShader, QShaderDescription>> deserializedShaderHash; // Shader deserialization is permanently cached
    {
        QMutexLocker lock(&cacheLock);

        QPair<QShader, QShaderDescription> deserializedShader;

        if (!deserializedShaderHash.contains(computeShader))
        {
            const auto [shader, description] = QSGComputeNode::deserializeShader(computeShader);
            if (!shader || !description)
                return false;
            deserializedShader = {*shader, *description};
            deserializedShaderHash[computeShader] = deserializedShader;
        }
        else
        {
            deserializedShader = deserializedShaderHash[computeShader];
        }

        if (computeNodeCache.contains(computeShader))
        {
            node = computeNodeCache.object(computeShader);

            bool cachedNodeCanBeUsed = false;
            if (node->m_lock.tryLock(0))
            {
                cachedNodeCanBeUsed = !node->m_inputDataDirty;
                node->m_lock.unlock();
            }

            if (cachedNodeCanBeUsed)
            {
                node->setWindow(window);
                ownedByCache = true;
            }
            else
            {
                // The node is not done yet for the previous input data, we can't reuse it
                node = new QSGComputeNode(&deserializedShader.first, &deserializedShader.second, window);
            }
        }
        else
        {
            node = new QSGComputeNode(&deserializedShader.first, &deserializedShader.second, window);
            if (computeNodeCache.insert(computeShader, node))
                ownedByCache = true;
            else
                node = new QSGComputeNode(&deserializedShader.first, &deserializedShader.second, window);
        }

        assert(node);
    }

    node->setInputData(inputData);

    const auto cb = [node, doneCallback, ownedByCache](const QVariant& result) {
        doneCallback(result);
        if (!ownedByCache)
            delete node;
    };

    if (node->computeAsync(dispatchParameters, cb))
    {
        return true;
    }
    else
    {
        if (!ownedByCache)
            delete node;
        return false;
    }
}

QVariant QSGComputeNode::compute(const QString &computeShader,
                                 const QVariant &inputData,
                                 QQuickWindow *window,
                                 const std::tuple<int, int, int> &dispatchParameters,
                                 int waitingTime,
                                 std::optional<std::reference_wrapper<QPointer<class QEventLoop>>> _eventLoop)
{
    QEventLoop eventLoop;

    if (_eventLoop)
        _eventLoop->get() = &eventLoop; // Weak pointer/reference

    std::unique_ptr<QTimer> watchdogTimer;
    if (waitingTime > 0)
    {
        watchdogTimer = std::make_unique<QTimer>();
        watchdogTimer->setSingleShot(true);
        watchdogTimer->setInterval(waitingTime);

        QObject::connect(watchdogTimer.get(), &QTimer::timeout, &eventLoop, [&eventLoop]() {
            // Bail out if it took too much time:
            eventLoop.exit(1);
        });
    }

    QVariant ret;

    QMutex finishLock;
    bool finished = false;
\
    if (QSGComputeNode::computeAsync(computeShader,
                                     inputData,
                                     window,
                                     dispatchParameters,
                                     [&ret, &finished, &eventLoop, &finishLock](const QVariant& output) {
                                         ret = output;

                                         {
                                             QMutexLocker lock(&finishLock);
                                             finished = true;
                                             eventLoop.exit(); // Returns from the event loop with 0 error code.
                                         }
                                     }))
    {
        {
            QMutexLocker lock(&finishLock);

            if (Q_LIKELY(!finished)) // It would be extremely unlikely for it to be already finished
            {
                if (watchdogTimer)
                    watchdogTimer->start();

                QMetaObject::invokeMethod(&eventLoop, [&lock]() {
                    lock.unlock();
                }, Qt::QueuedConnection);

                eventLoop.exec();

                // This method is static, so it is not relevant for now, but if it ever becomes an ordinary method
                // you can not access members beyond this point (`exec()` returns). This is because the sub event
                // loop may be still running when the node instance is destroyed.
            }
        }
    }

    return ret;
}

bool QSGComputeNode::setShader(const QString &computeShader)
{
    if (!computeShader.isEmpty())
    {
        const auto [shader, shaderDescription] = deserializeShader(computeShader);
        if (!shader || !shaderDescription)
            return false;
        return setShader(shader.get(), shaderDescription.get());
    }
    else
    {
        setShader(nullptr, nullptr);
    }

    return false;
}

bool QSGComputeNode::setShader(QShader *computeShader, const QShaderDescription *computeShaderDescription)
{
    QMutexLocker locker(&m_lock);

    // Free the resources:

    const auto releaseRhiBoundResources = [pipeline = m_pipeline.get(),
                                           resourceBindings = m_resourceBindings.get(),
                                           inputBuffer = m_inputBuffer.get(),
                                           inputTexture = m_inputTexture.get(),
                                           inputSampler = m_inputSampler.get(),
                                           resourceUpdateBatch = m_resourceUpdateBatch,
                                           outputBuffer = m_outputBuffer.get(),
                                           outputTexture = m_outputTexture.get()] () {
        if (resourceUpdateBatch)
            resourceUpdateBatch->release();

        if (pipeline)
            pipeline->deleteLater();

        if (resourceBindings)
            resourceBindings->deleteLater();

        if (inputBuffer)
            inputBuffer->deleteLater();

        if (inputTexture)
            inputTexture->deleteLater();

        if (inputSampler)
            inputSampler->deleteLater();

        if (outputBuffer)
            outputBuffer->deleteLater();

        if (outputTexture)
            outputTexture->deleteLater();
    };

    bool needToDestroyRhiBoundObjectsOurself = true;
    if (m_window)
    {
        const auto [rhi, swapChain] = getRhiAndSwapChainFromWindow(m_window);

        if (rhi)
        {
            const auto rhiThread = rhi->thread();

            if (QThread::currentThread() != rhiThread)
            {
                // TODO: Investigate whether making this context object permanent makes sense:
                const auto rhiContextObject = new QObject;
                rhiContextObject->moveToThread(rhiThread);

                // Does not need to be blocking queued:
                QMetaObject::invokeMethod(rhiContextObject, [releaseRhiBoundResources, rhiContextObject]() {
                    releaseRhiBoundResources();
                    rhiContextObject->deleteLater();
                }, Qt::QueuedConnection);
            }
            else
            {
                releaseRhiBoundResources();
            }

            needToDestroyRhiBoundObjectsOurself = false;
        }
    }

    if (needToDestroyRhiBoundObjectsOurself)
    {
        // This is not ideal, but if the window or rhi is gone, we need to destroy the objects
        // ourselves to prevent leak.
        m_pipeline.reset();
        m_resourceBindings.reset();
        m_inputBuffer.reset();
        m_inputTexture.reset();
        m_inputSampler.reset();
        m_outputBuffer.reset();
        m_outputTexture.reset();
    }
    else
    {
        m_pipeline.release();
        m_resourceBindings.release();
        m_inputBuffer.release();
        m_inputTexture.release();
        m_inputSampler.release();
        m_outputBuffer.release();
        m_outputTexture.release();
    }

    m_resourceUpdateBatch = nullptr;
    m_inputDataDirty = true;

    m_shaderStage.reset();
    m_shader.reset();

    m_inputBufferSize = 0;
    m_outputBufferSize = 0;

    if (!computeShader || !computeShaderDescription)
        return true;

    // If provided, now adjust with the new shader:

    const QList<QShaderDescription::StorageBlock> storageBlocks = computeShaderDescription->storageBlocks();
    const QList<QShaderDescription::InOutVariable> storageImages = computeShaderDescription->storageImages();

    if (!storageBlocks.isEmpty())
    {
        assert(storageBlocks.size() == 2); // The shader must have exactly two storage buffers (input and output)

        for (const auto& block : storageBlocks)
        {
            if (block.binding == 0) // Input buffer
            {
                m_inputBufferSize = block.knownSize;
                assert(m_inputBufferSize > 0); // Invalid size
            }
            else if (block.binding == 1) // Output buffer
            {
                m_outputBufferSize = block.knownSize;
                assert(m_outputBufferSize > 0); // Invalid size
            }
        }
    }
    else if (!storageImages.isEmpty())
    {
        const auto size = storageImages.size();

        // The shader must have exactly one or two storage images (input and output),
        // but since RGBA8 texture load support is not a ubiquitous feature (unlike
        // store), a sampler can be used to read from the image through `texelFetch()`,
        // instead of `imageLoad()`.

        assert(size == 1 || size == 2);

        if (size == 1)
        {
            assert(computeShaderDescription->combinedImageSamplers().size() == 1);
            m_inputSamplerRequired = true;
        }
        else
        {
            m_inputSamplerRequired = false;
        }
    }

    m_shader.reset(new QShader(*computeShader));

    return true;
}

void QSGComputeNode::setWindow(QQuickWindow *window)
{
    QMutexLocker locker(&m_lock);

    m_window = window;
}

void QSGComputeNode::setInputData(const QVariant &data)
{
    QMutexLocker locker(&m_lock);

    m_inputData = data;
    m_inputDataDirty = true;
}

ComputeRenderJob *QSGComputeNode::newComputeRenderJob(const std::tuple<int, int, int> &dispatchParameters, std::function<void (const QVariant &)> doneCallback)
{
    assert(isPrepared());

    QMutexLocker locker(&m_lock);

    assert(m_window); // `QSGComputeNode::computeAsync()` can only be called with a valid window.
    assert(doneCallback); // Meaningless otherwise

    const auto [rhi, swapChain] = getRhiAndSwapChainFromWindow(m_window);

    assert(rhi);

    // We assume for the moment that we can call `QRhi::isFeatureSupported()` in non-rhi thread:
    if (!rhi->isFeatureSupported(QRhi::Compute))
    {
        qCritical() << "QSGComputeRenderNode::computeAsync(): Compute shader is not supported, if you are using OpenGL, make sure to use at least OpenGL 4.3 or OpenGL ES 3.1!";
        return nullptr;
    }

    assert(swapChain);
    const auto cb = swapChain->currentFrameCommandBuffer();
    assert(cb);

    const auto ret = new ComputeRenderJob(cb,
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
                                          rhi,
#endif
                                          m_pipeline.get(),
                                          m_resourceBindings.get(),
                                          dispatchParameters,
                                          m_outputBuffer.get(),
                                          m_outputTexture.get(),
                                          doneCallback,
                                          m_resourceUpdateBatch);

    m_resourceUpdateBatch = nullptr;

    return ret;
}

QPair<std::unique_ptr<QShader>, std::unique_ptr<QShaderDescription>> QSGComputeNode::deserializeShader(const QString &computeShader)
{
    assert(!computeShader.isEmpty());

    // Qt Rhi `CustomRenderNode` example:
    QFile file;
    file.setFileName(computeShader);
    if (!file.open(QFile::ReadOnly))
    {
        qCritical() << "QSGComputeRenderNode::setShader(): Failed to load the open shader file" << computeShader;
        return {nullptr, nullptr};
    }

    const auto shader = QShader::fromSerialized(file.readAll());
    if (!shader.isValid())
    {
        qCritical() << "QSGComputeRenderNode::setShader(): Failed to deserialize the shader" << computeShader;
        return {nullptr, nullptr};
    }

    return {std::make_unique<QShader>(shader), std::make_unique<QShaderDescription>(shader.description())};
}

bool QSGComputeNode::prepare()
{
    assert(m_window);

    const auto [rhi, swapChain] = getRhiAndSwapChainFromWindow(m_window);

    assert(rhi);

    // This method must be called in RHI/rendering thread:
    assert(QThread::currentThread() == rhi->thread());

    QMutexLocker locker(&m_lock);

    if (!m_inputBuffer && m_inputBufferSize > 0)
    {
        m_inputBuffer.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::StorageBuffer, m_inputBufferSize));
        if (Q_UNLIKELY(!m_inputBuffer || !m_inputBuffer->create()))
        {
            qCritical() << "QSGComputeRenderNode::computeAsync(): Input buffer creation failed!";
            m_inputBuffer.reset();
            return false;
        }
    }

    QRhiResourceUpdateBatch *resourceUpdates = nullptr;

    if (m_inputDataDirty)
    {
        resourceUpdates = rhi->nextResourceUpdateBatch();
        assert(resourceUpdates);

        switch (m_inputData.typeId())
        {
        case QMetaType::QByteArray:
        {
            const auto inputData = m_inputData.toByteArray(); // No worry about copy, implicitly shared
            assert(inputData.size() == m_inputBuffer->size());
            resourceUpdates->uploadStaticBuffer(m_inputBuffer.get(), inputData.constData());
            break;
        }
        case QMetaType::QImage:
        {
            const auto inputImage = m_inputData.value<QImage>();
            assert(!inputImage.isNull());

            if (!m_inputTexture || m_inputTexture->pixelSize() != inputImage.size())
            {
                m_inputTexture.reset(rhi->newTexture(QRhiTexture::RGBA8,
                                                     inputImage.size(),
                                                     1,
                                                     QRhiTexture::UsedWithLoadStore));
                m_resourceBindings.reset(); // Will be regenerated below
            }

            if (Q_UNLIKELY(!m_inputTexture || !m_inputTexture->create()))
            {
                qCritical() << "QSGComputeRenderNode::computeAsync(): Input texture creation failed!";
                m_inputData.clear();
                m_inputTexture.reset();
                return false;
            }

            if (m_inputSamplerRequired)
            {
                if (!m_inputSampler)
                {
                    m_inputSampler.reset(rhi->newSampler(QRhiSampler::Nearest,
                                                         QRhiSampler::Nearest,
                                                         QRhiSampler::None,
                                                         QRhiSampler::ClampToEdge,
                                                         QRhiSampler::ClampToEdge,
                                                         QRhiSampler::ClampToEdge));
                    m_resourceBindings.reset(); // Will be regenerated below
                }

                if (Q_UNLIKELY(!m_inputSampler || !m_inputSampler->create()))
                {
                    qCritical() << "QSGComputeRenderNode::computeAsync(): Input texture sampler creation failed!";
                    m_inputData.clear();
                    m_inputTexture.reset();
                    m_inputSampler.reset();
                    return false;
                }
            }
            else
            {
                m_inputSampler.reset();
                m_resourceBindings.reset(); // Will be regenerated below
            }

            resourceUpdates->uploadTexture(m_inputTexture.get(), inputImage);

            if (!m_outputTexture || m_outputTexture->pixelSize() != inputImage.size())
            {
                m_outputTexture.reset(rhi->newTexture(QRhiTexture::RGBA8,
                                                      inputImage.size(),
                                                      1,
                                                      QRhiTexture::UsedWithLoadStore | QRhiTexture::UsedAsTransferSource));

                m_resourceBindings.reset(); // Will be regenerated below
            }

            if (Q_UNLIKELY(!m_outputTexture || !m_outputTexture->create()))
            {
                qCritical() << "QSGComputeRenderNode::computeAsync(): Output texture creation failed!";
                m_outputTexture.reset();
                return false;
            }

            break;
        }
        default:
        {
            qCritical() << "QSGComputeRenderNode::computeAsync(): Only `QByteArray` and `QImage` types are supported for now as input data.";
            return false;
        }
        }

        if (m_resourceUpdateBatch)
            m_resourceUpdateBatch->release();

        m_resourceUpdateBatch = resourceUpdates;

        m_inputData.clear(); // "data is copied and can safely be destroyed or changed once this function returns."
        // The dirty flag set false at the end of this method.
    }

    if (!m_outputBuffer && m_outputBufferSize > 0)
    {
        m_outputBuffer.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::StorageBuffer, m_outputBufferSize));
        if (Q_UNLIKELY(!m_outputBuffer || !m_outputBuffer->create()))
        {
            qCritical() << "QSGComputeRenderNode::computeAsync(): Output buffer creation failed!";
            m_outputBuffer.reset();
            return false;
        }
    }

    if (!m_resourceBindings)
    {
        m_resourceBindings.reset(rhi->newShaderResourceBindings());
        m_pipeline.reset(); // Will be generated below

        if (Q_UNLIKELY(!m_resourceBindings))
        {
            qCritical() << "QSGComputeRenderNode::computeAsync(): QRhi::newShaderResourceBindings() returned null pointer!";
            return false;
        }

        QRhiShaderResourceBinding in;
        QRhiShaderResourceBinding out;

        if (m_inputBuffer)
            in = QRhiShaderResourceBinding::bufferLoad(0, QRhiShaderResourceBinding::ComputeStage, m_inputBuffer.get());
        else if (m_inputTexture)
        {
            if (m_inputSampler)
                in = QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::ComputeStage, m_inputTexture.get(), m_inputSampler.get());
            else
                in = QRhiShaderResourceBinding::imageLoad(0, QRhiShaderResourceBinding::ComputeStage, m_inputTexture.get(), 0);
        }
        else
            Q_UNREACHABLE();

        if (m_outputBuffer)
            out = QRhiShaderResourceBinding::bufferLoadStore(1, QRhiShaderResourceBinding::ComputeStage, m_outputBuffer.get());
        else if (m_outputTexture)
            out = QRhiShaderResourceBinding::imageLoadStore(1, QRhiShaderResourceBinding::ComputeStage, m_outputTexture.get(), 0);
        else
            Q_UNREACHABLE();

        m_resourceBindings->setBindings({in, out});

        if (Q_UNLIKELY(!m_resourceBindings->create()))
        {
            qCritical() << "QSGComputeRenderNode::computeAsync(): QRhiShaderResourceBindings::create() returned false!";
            m_resourceBindings.reset();
            return false;
        }
    }

    if (!m_shaderStage)
    {
        assert(m_shader);
        m_shaderStage = std::make_unique<QRhiShaderStage>(QRhiShaderStage::Compute, *m_shader);
    }

    if (!m_pipeline)
    {
        m_pipeline.reset(rhi->newComputePipeline());

        if (Q_UNLIKELY(!m_pipeline))
        {
            qCritical() << "QSGComputeRenderNode::computeAsync(): QRhi::newComputePipeline() returned null pointer!";
            return false;
        }

        assert(m_shaderStage); // You can not call `QSGComputeRenderNode::computeAsync()` without valid shader bound.
        assert(m_resourceBindings);

        m_pipeline->setShaderStage(*m_shaderStage.get());
        m_pipeline->setShaderResourceBindings(m_resourceBindings.get());

        if (Q_UNLIKELY(!m_pipeline->create()))
        {
            qCritical() << "QSGComputeRenderNode::computeAsync(): QRhiComputePipeline::create() returned false!";
            m_pipeline.reset();
            return false;
        }
    }

    m_inputDataDirty = false;

    return true;
}

bool QSGComputeNode::isPrepared() const
{
    QMutexLocker locker(&m_lock);

    return ((m_inputBuffer || (m_inputTexture && (!m_inputSamplerRequired || m_inputSampler))) && !m_inputDataDirty &&
            (m_outputBuffer || m_outputTexture) &&
            m_resourceBindings &&
            m_pipeline);
}
