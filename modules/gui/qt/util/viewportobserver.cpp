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
#include "viewportobserver.hpp"

#include <QQmlComponent>

ViewportObserver::ViewportObserver(QQuickItem *parent)
    : QQuickItem(parent)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
    setFlag(ItemObservesViewport);
#endif

    if constexpr (QT_VERSION >= QT_VERSION_CHECK(6, 9, 0))
    {
        connect(this, &ViewportObserver::preventOverdrawChanged, this, [this](bool preventOverdraw) {
            if (preventOverdraw)
            {
                setPreferAdjustingVisibilityOverOpacity(preferAdjustingVisibilityOverOpacity());
            }
            else
            {
                if (m_activeBinding)
                    delete m_activeBinding;
            }
        });
    }
    else
    {
        static bool messageAlreadyEmitted = false;
        if (!messageAlreadyEmitted)
        {
            if constexpr (QT_VERSION < QT_VERSION_CHECK(6, 3, 0))
                qDebug() << "ViewportObserver will not be functional, because Qt version is lower than 6.3.0!";
            else
                qDebug() << "ViewportObserver: prevent overdraw will not be functional, because Qt version is lower than 6.9.0.";

            messageAlreadyEmitted = true;
        }
    }
}

bool ViewportObserver::preferAdjustingVisibilityOverOpacity() const
{
    if (m_qmlVisibleBinding)
        return true;
    else
        return false; // Default value (whether or not `m_qmlOpacityBinding` exists)
}

void ViewportObserver::setPreferAdjustingVisibilityOverOpacity(bool preferVisibilityChange)
{
    if constexpr (QT_VERSION < QT_VERSION_CHECK(6, 9, 0))
        return; // not applicable

    if (!m_preventOverdraw)
        return;

    if (m_activeBinding && (preferAdjustingVisibilityOverOpacity() == preferVisibilityChange))
        return;

    if (!m_qmlBindingFactory)
    {
        const auto engine = qmlEngine(this);
        assert(engine); // Do not call this method without qml engine bound.

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        m_qmlBindingFactory = new QQmlComponent(engine, QLatin1String("QtQml"), QLatin1String("Binding"), this);
#else
        m_qmlBindingFactory = new QQmlComponent(engine, this);
        m_qmlBindingFactory->setData(QByteArrayLiteral("import QtQml; Binding { }"), { });
#endif

        assert(m_qmlBindingFactory->isReady()); // `Binding` is Qt built-in core type, it must be available.
    }

    if (preferVisibilityChange)
    {
        assert(!m_qmlVisibleBinding);

        m_qmlVisibleBinding = m_qmlBindingFactory->createWithInitialProperties({{QStringLiteral("property"), QStringLiteral("visible")},
                                                                                {QStringLiteral("value"), false},
                                                                                {QStringLiteral("when"), false},
                                                                                {QStringLiteral("target"), QVariant::fromValue(parentItem()) } });

        if (m_qmlOpacityBinding)
            delete m_qmlOpacityBinding;

        m_activeBinding = m_qmlVisibleBinding;
    }
    else
    {
        assert(!m_qmlOpacityBinding);

        m_qmlOpacityBinding = m_qmlBindingFactory->createWithInitialProperties({{QStringLiteral("property"), QStringLiteral("opacity")},
                                                                                {QStringLiteral("value"), 0.0},
                                                                                {QStringLiteral("when"), false},
                                                                                {QStringLiteral("target"), QVariant::fromValue(parentItem()) } });

        if (m_qmlVisibleBinding)
            delete m_qmlVisibleBinding;

        m_activeBinding = m_qmlOpacityBinding;
    }

    onTransformChanged();

    emit preferAdjustingVisibilityOverOpacityChanged(preferVisibilityChange);
}

void ViewportObserver::componentComplete()
{
    // At this point, we are sure that there is qml engine bound. We are also sure that the engine is bound
    // if the property was adjusted prior component is complete, provided that it is done in QML.
    if (!m_activeBinding && m_preventOverdraw) // `setPreferAdjustingVisibilityOverOpacity()` may have been called before by the QML engine.
        setPreferAdjustingVisibilityOverOpacity(preferAdjustingVisibilityOverOpacity()); // Default value

    QQuickItem::componentComplete();
}

void ViewportObserver::itemChange(ItemChange change, const ItemChangeData& changeData)
{
    QQuickItem::itemChange(change, changeData);

    switch (change)
    {
    case ItemChange::ItemParentHasChanged:
    {
        if (Q_LIKELY(m_activeBinding))
        {
            m_activeBinding->setProperty("target", QVariant::fromValue(changeData.item));
            [[fallthrough]]; // Should handle transform change here as well (just in case it is not implicitly signalled).
        }
        else
        {
            break;
        }
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    case ItemChange::ItemTransformHasChanged:
    {
        onTransformChanged();
        break;
    }
#endif
    default:
        break;
    }
}

void ViewportObserver::onTransformChanged()
{
    static const QMetaMethod clipRectChangedSignal = QMetaMethod::fromSignal(&ViewportObserver::clipRectChanged);
    if (!m_activeBinding && !QObject::isSignalConnected(clipRectChangedSignal))
        return;

    const QRectF cRect = clipRect();

    if (cRect != m_oldClipRect)
    {
        emit clipRectChanged(cRect);
        m_oldClipRect = cRect;
    }

    if (m_activeBinding)
    {
        const bool outsideViewport = cRect.isNull();
        m_activeBinding->setProperty("when", outsideViewport);
    }
}
