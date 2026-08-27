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
#ifndef QSGCOMPUTENODE_HPP
#define QSGCOMPUTENODE_HPP

#include <QSGNode>

#include <QVariant>
#include <QPointer>
#include <QMutex>

#include <memory>
#include <optional>
#include <functional>
#include <tuple>

class QShader;
class QShaderDescription;
class QRhiShaderStage;
class QQuickWindow;
class QRhiBuffer;
class QRhiTexture;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiComputePipeline;
class QRhiResourceUpdateBatch;
class QEventLoop;

class ComputeRenderJob;

// You can use this node offload heavy computations to GPU using Qt Rhi,
// without needing to create offscreen frames or a whole new rhi setup.
// The compute pass is provided to the current frame in Qt Scene Graph at
// appropriate time, and a new frame is implicitly scheduled through
// `QQuickWindow::update()`. Note that the output buffer can also be read
// in the shader, if required, since load/store is used instead of just
// store.
class QSGComputeNode : public QSGNode
{
public:
    // Note that we only allow `qsb` files for now, because we don't want to
    // link to `qtshadertools` libs (which increases the size greatly in
    // static linking (Windows) case) to bake shaders at run time. This is
    // also the case with other shaders at the moment, though arguably
    // online/run-time compilation may be more useful with compute shaders,
    // so the door is not closed. On Linux where Qt libs are linked dynamically,
    // we could bake the shader using `QShaderBaker`.
    // NOTE: The node can be created in any thread.
    QSGComputeNode(const QString& computeShader = {}, QQuickWindow *window = nullptr);
    QSGComputeNode(QShader *computeShader, const QShaderDescription *computeShaderDescription, QQuickWindow *window = nullptr);

    // NOTE: The node can be deleted in any thread:
    ~QSGComputeNode();

    // Returns true if QRhi implementation supports compute,
    // and the window has valid swapchain (may not the be case
    // with offscreen rendering, such as when `--qt-compositor=x11`
    // is used where direct OpenGL is used without `QRhi`).
    // Compute support is only a concern with OpenGL, where
    // OpenGL 4.3 or OpenGL ES 3.1 is necessary to have the
    // support, since in other cases (HLSL 5.0 and MSL 1.2) the
    // minimum version already provides the support.
    static bool supported(const QQuickWindow *window);

    // Asynchronously compute, and call the callback afterwards.
    // WARNING: Window must be valid when this method is called.
    // NOTE: This method can be called in any thread:
    bool computeAsync(const std::tuple<int, int, int>& dispatchParameters,
                      std::function<void(const QVariant&)> doneCallback);

    // For convenience:
    bool computeAsync(const QString &computeShader,
                      const std::tuple<int, int, int>& dispatchParameters,
                      std::function<void(const QVariant&)> doneCallback)
    {
        if (setShader(computeShader))
            return computeAsync(dispatchParameters, doneCallback);
        return false;
    }

    // This method caches the compute nodes with the shader file name
    // as its key.
    // For convenience:
    static bool computeAsync(const QString &computeShader,
                             const QVariant& inputData,
                             QQuickWindow *window,
                             const std::tuple<int, int, int>& dispatchParameters,
                             std::function<void(const QVariant&)> doneCallback);

    // For convenience, it runs an event loop with a watchdog timer to bail out
    // if it takes time more than `waitingTime` and calls `computeAsync()`. If
    // you can, try to avoid using this method in favor of `::computeAsync()`.
    // If `waitingTime` is 0, the method can potentially wait indefinitely before
    // returning. The parameter `eventLoop` that provides a weak reference to the
    // internal/sub event loop can be used to exit the event loop manually, in
    // cases where you call `::compute()` needs to end prematurely, such as when
    // the object that called `::compute()` is dying, and the method beyond the
    // the call accesses class members.
    static QVariant compute(const QString &computeShader,
                            const QVariant& inputData,
                            QQuickWindow *window,
                            const std::tuple<int, int, int>& dispatchParameters,
                            int waitingTime = 20,
                            std::optional<std::reference_wrapper<QPointer<QEventLoop>>> eventLoop = {});

    // NOTE: Both can be called in any thread:
    bool setShader(const QString& computeShader);
    // You can delete `QShader` or `QShaderDescription` after the call,
    // pointer is only used for forward declaration, this method has copy
    // semantics:
    bool setShader(QShader *computeShader, const QShaderDescription *computeShaderDescription);

    // NOTE: Can be called in any thread:
    void setWindow(QQuickWindow *window);

    // If you are using storage blocks, the input data you provide must be
    // aligned and the same size with the shader's definition of the block.
    // Please refer to std140/std430 layout rules and documentation.
    // It is allowed to call this method repeatedly, but in image case, it
    // is not advised to use different images with varying size since that
    // would require creating a new `QRhiTexture` every time.
    // NOTE: This method can be called in any thread:
    void setInputData(const QVariant& data);

protected:
    // NOTE: Caller takes the ownership of the job created.
    // NOTE: This method must be called when preparations are already done.
    ComputeRenderJob* newComputeRenderJob(const std::tuple<int, int, int> &dispatchParameters, std::function<void (const QVariant &)> doneCallback);

    static QPair<std::unique_ptr<QShader>, std::unique_ptr<QShaderDescription>> deserializeShader(const QString& computeShader);

private:
    // WARNING: Must be called in the RHI thread, particularly if preparations need to be done.
    bool prepare();

    // NOTE: Can be called in any thread:
    bool isPrepared() const;

private:
    // TODO: Investigate if `QReadWriteLock` instead makes more sense:
    mutable QMutex m_lock;

    std::unique_ptr<QShader> m_shader;
    std::unique_ptr<QRhiShaderStage> m_shaderStage;
    int m_inputBufferSize = 0; // Not relevant if image/texture is used instead
    int m_outputBufferSize = 0; // Not relevant if image/texture is used instead

    QPointer<QQuickWindow> m_window;
    std::unique_ptr<QRhiBuffer> m_inputBuffer;
    std::unique_ptr<QRhiTexture> m_inputTexture;
    std::unique_ptr<QRhiSampler> m_inputSampler;
    bool m_inputSamplerRequired = false;
    std::unique_ptr<QRhiBuffer> m_outputBuffer;
    std::unique_ptr<QRhiTexture> m_outputTexture;
    std::unique_ptr<QRhiShaderResourceBindings> m_resourceBindings;
    std::unique_ptr<QRhiComputePipeline> m_pipeline;
    // This is not unique pointer because we can't manage its lifetime ourselves unlike the other RHI objects,
    // see the docs (https://doc.qt.io/qt-6/qrhi.html#nextResourceUpdateBatch):
    QRhiResourceUpdateBatch *m_resourceUpdateBatch = nullptr;

    QVariant m_inputData; // Can be `QByteArray` or `QImage`
    bool m_inputDataDirty = false;
};

#endif // QSGCOMPUTENODE_HPP
