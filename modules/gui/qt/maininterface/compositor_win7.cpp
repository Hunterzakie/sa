/*****************************************************************************
 * Copyright (C) 2020 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#include "compositor_win7.hpp"

#include <QApplication>
#include <QLibrary>

#include "mainctx_win32.hpp"
#include "mainui.hpp"

#include <vlc_window.h>

#include <dwmapi.h>

using namespace vlc;

int CompositorWin7::windowEnable(const vlc_window_cfg_t *)
{
    commonWindowEnable();
    return VLC_SUCCESS;
}

void CompositorWin7::windowDisable()
{
    commonWindowDisable();
}


CompositorWin7::CompositorWin7(qt_intf_t *p_intf, QObject* parent)
    : CompositorVideo(p_intf, parent)
{
}

CompositorWin7::~CompositorWin7()
{
}

bool CompositorWin7::init()
{
    {
        const QString& platformName = qApp->platformName();
        if (!(platformName == QLatin1String("windows") || platformName == QLatin1String("direct2d")))
            return false;
    }

    return true;
}

bool CompositorWin7::makeMainInterface(MainCtx* mainCtx, std::function<void (QQuickWindow *)> aboutToShowQuickWindowCallback)
{
    m_mainCtx = mainCtx;

    /*
     * m_stable is not attached to the main interface because dialogs are attached to the mainCtx
     * and showing them would raise the video widget above the interface
     */
    m_videoWindow = new QWindow;
    m_videoWindow->setFlags(Qt::Tool | Qt::FramelessWindowHint);
    m_stable = new QWindow(m_videoWindow);

    m_videoWindow->setVisible(true);
    m_stable->setVisible(true);

    m_videoWindowHWND = (HWND)m_videoWindow->winId();

    BOOL excluseFromPeek = TRUE;
    DwmSetWindowAttribute(m_videoWindowHWND, DWMWA_EXCLUDED_FROM_PEEK, &excluseFromPeek, sizeof(excluseFromPeek));
    DwmSetWindowAttribute(m_videoWindowHWND, DWMWA_DISALLOW_PEEK, &excluseFromPeek, sizeof(excluseFromPeek));

    m_quickView = std::make_unique<QQuickView>();
    m_quickView->setResizeMode(QQuickView::SizeRootObjectToView);
    m_quickView->setColor(QColor(Qt::transparent));

    m_quickView->installEventFilter(this);
    m_nativeEventFilter = std::make_unique<Win7NativeEventFilter>();
    qApp->installNativeEventFilter(m_nativeEventFilter.get());
    connect(m_nativeEventFilter.get(), &Win7NativeEventFilter::windowStyleChanged,
            this, &CompositorWin7::resetVideoZOrder);

    m_quickView->create();
    m_quickWindowHWND = (HWND)m_quickView->winId();

    const bool ret = commonGUICreate(m_quickView.get(), m_quickView.get(), CompositorVideo::CAN_SHOW_PIP);

    if (ret)
    {
        if (aboutToShowQuickWindowCallback)
            aboutToShowQuickWindowCallback(m_quickView.get());
        m_quickView->setVisible(true);
    }

    return ret;
}

void CompositorWin7::destroyMainInterface()
{
    commonIntfDestroy();
    delete m_videoWindow;
    m_videoWindow = nullptr;
}

void CompositorWin7::unloadGUI()
{
    m_interfaceWindowHandler.reset();
    m_quickView.reset();
    commonGUIDestroy();
}

bool CompositorWin7::setupVoutWindow(vlc_window_t *p_wnd, VoutDestroyCb destroyCb)
{
    if (m_wnd)
        return false;

    BOOL isCompositionEnabled;
    HRESULT hr = DwmIsCompositionEnabled(&isCompositionEnabled);

    //composition is disabled, video can't be seen through the interface,
    //so we fallback to a separate window.
    if (FAILED(hr) || !isCompositionEnabled)
        return false;

    commonSetupVoutWindow(p_wnd, destroyCb);
    p_wnd->type = VLC_WINDOW_TYPE_HWND;
    p_wnd->handle.hwnd = (HWND)m_stable->winId();
    p_wnd->display.x11 = nullptr;
    return true;
}

QWindow *CompositorWin7::interfaceMainWindow() const
{
    return m_quickView.get();
}

Compositor::Type CompositorWin7::type() const
{
    return Compositor::Win7Compositor;
}

QQuickItem * CompositorWin7::activeFocusItem() const /* override */
{
    return m_quickView->activeFocusItem();
}

bool CompositorWin7::eventFilter(QObject*, QEvent* ev)
{
    if (!m_videoWindow || !m_quickView)
        return false;

    switch (ev->type())
    {
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::ApplicationStateChange:
        m_videoWindow->setGeometry(m_quickView->geometry());
        resetVideoZOrder();
        break;
    case QEvent::WindowStateChange:
        if (m_quickView->windowStates() & Qt::WindowMinimized)
            m_videoWindow->hide();
        else
        {
            m_videoWindow->setVisible(true);
            m_videoWindow->setGeometry(m_quickView->geometry());
            resetVideoZOrder();
        }
        break;

    case QEvent::FocusIn:
        resetVideoZOrder();
        break;
    case QEvent::Show:
        m_videoWindow->setVisible(true);
        resetVideoZOrder();
        break;
    case QEvent::Hide:
        m_videoWindow->hide();
        break;
    default:
        break;
    }

    return false;
}

void CompositorWin7::commitSurface()
{
    if (!m_stable)
        return;

    if (m_pendingPosition && m_pendingSize)
    {
        m_stable->setGeometry(m_pendingPosition->x(), m_pendingPosition->y(),
                              m_pendingSize->width(), m_pendingSize->height());
        m_pendingPosition.reset();
        m_pendingSize.reset();
    }
    else
    {
        if (m_pendingSize)
        {
            m_stable->resize(*m_pendingSize);
            m_pendingSize.reset();
        }
        else if (m_pendingPosition)
        {
            m_stable->setPosition(*m_pendingPosition);
            m_pendingPosition.reset();
        }
    }
}

void CompositorWin7::resetVideoZOrder()
{
    //Place the video wdiget right behind the interface
    HWND bottomHWND = m_quickWindowHWND;
    HWND currentHWND = bottomHWND;
    while (currentHWND != nullptr)
    {
        bottomHWND = currentHWND;
        currentHWND = GetWindow(bottomHWND, GW_OWNER);
    }

    SetWindowPos(
        m_videoWindowHWND,
        bottomHWND,
        0,0,0,0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
    );
}

void CompositorWin7::onSurfacePositionChanged(const QPointF& position)
{
    const QPointF point = position / m_stable->devicePixelRatio();
    m_pendingPosition = QPoint{static_cast<int>(point.x()), static_cast<int>(point.y())};
}

void CompositorWin7::onSurfaceSizeChanged(const QSizeF& size)
{
    const QSizeF area = (size / m_stable->devicePixelRatio());
    m_pendingSize = QSize{static_cast<int>(std::ceil(area.width())), static_cast<int>(std::ceil(area.height()))};
}


Win7NativeEventFilter::Win7NativeEventFilter(QObject* parent)
    : QObject(parent)
{
}

//parse native events that are not reported by Qt
bool Win7NativeEventFilter::nativeEventFilter(const QByteArray&, void* message, qintptr*)
{
    MSG * msg = static_cast<MSG*>( message );

    switch( msg->message )
    {
        //style like "always on top" changed
        case WM_STYLECHANGED:
            emit windowStyleChanged();
            break;
    }
    return false;
}
