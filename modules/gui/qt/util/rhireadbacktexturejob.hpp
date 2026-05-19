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
#ifndef RHIREADBACKTEXTUREJOB_HPP
#define RHIREADBACKTEXTUREJOB_HPP

#include <QRunnable>
#include <QPointer>

#include <optional>

class QQuickWindow;
class QQuickItem;

// This job executes texture read back operation through rhi on
// a texture provider item. If the callback is of type `QJSValue`,
// it is guaranteed that it will be called in JS engine's thread.
// It is also guaranteed that, although `QJSValue` crosses engine
// thread boundaries (to scene graph thread), the last reference
// holding it will be dropped in the engine's thread. This assures
// that what is done here is safe (see the note in `QJSManagedValue`).
// If the callback is of type `std::function<void(const QImage&)>`,
// it will be called in the context's thread, provided that context
// is provided. Otherwise, it will be called arbitrarily from the
// scene graph thread. If you only want to use the image in the
// callback, you do not need to (and should not) provide the context.
// Providing context is recommended with `QJSValue` callback, since
// otherwise the context object is determined to be texture provider
// item (which should be safe, but still not recommended). Note that
// if the context (provided, or texture provider item that we determine)
// is not bound to a QML engine, the job will fail. Also note that
// it is not asserted that the job will succeed, if the job fails
// for non-asserted reason, an error message will be printed and
// no error will be thrown.

// This job should be executed in render/scene graph thread during
// synchronization. You may use `QQuickWindow::scheduleRenderJob()`
// with `QQuickWindow::RenderStage::AfterSynchronizingStage` or
// `QQuickWindow::RenderStage::BeforeSynchronizingStage`.
template<typename CallbackType>
class RhiReadBackTextureJob : public QRunnable
{
    const QPointer<QQuickWindow> m_window;
    QPointer<QObject> m_context;
    // Either a callable `QJSValue` with `ImageRenderResult` as the sole parameter,
    // or a function/functor such as `std::function<void(const QImage&)>`:
    const CallbackType m_callback;
    const QPointer<QQuickItem> m_textureProviderItem;

    mutable std::optional<QString> m_jobId;

public:
    explicit RhiReadBackTextureJob(QPointer<QQuickWindow> window,
                                   QPointer<QQuickItem> textureProviderItem,
                                   QPointer<QObject> context,
                                   CallbackType callback);

    void run() override;

private:
    QString jobId() const;
};

#endif // RHIREADBACKTEXTUREJOB_HPP
