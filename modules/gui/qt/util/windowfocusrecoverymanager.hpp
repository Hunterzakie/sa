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
#ifndef WINDOWFOCUSRECOVERYMANAGER_HPP
#define WINDOWFOCUSRECOVERYMANAGER_HPP

#include <QQuickItem>
#include <QPointer>
#include <QQuickWindow>

#if __has_include(<QtQuick/private/qquickitem_p.h>)
#define HAVE_QT_QUICK_PRIVATE
#include <QtQuick/private/qquickitem_p.h>
#endif

class WindowFocusRecoveryManager : public QQuickItem
{
    Q_OBJECT

    QML_ELEMENT

public:
    explicit WindowFocusRecoveryManager(QQuickItem *parent = nullptr)
        : QQuickItem(parent)
    { }

protected:
    void itemChange(QQuickItem::ItemChange change, const QQuickItem::ItemChangeData &value) override
    {
        if (change == ItemSceneChange)
        {
            m_activeFocusItem.clear();

            if (m_window)
            {
                disconnect(m_window, nullptr, this, nullptr);
            }

            const auto newWindow = value.window;

            m_window = newWindow;

            if (newWindow)
            {
                qDebug() << "WindowFocusRecoveryManager: global focus recovery is engaged for window:" << newWindow;

                connect(newWindow, &QQuickWindow::activeFocusItemChanged, this, [this]() {
                    if (m_activeFocusItem)
                    {
                        disconnect(m_activeFocusItem, nullptr, this, nullptr);
                    }

                    assert(m_window);

                    m_activeFocusItem = m_window->activeFocusItem();

                    if (!m_activeFocusItem)
                        return;

                    connect(m_activeFocusItem, &QQuickItem::visibleChanged, this, [this, item = m_activeFocusItem]() {
                        if (Q_UNLIKELY(!item))
                            return;

                        // This is intentionally a queued connection (to respect if there is specialized focus
                        // recovery), so we need to confirm if the active focus item is still the target item:
                        const auto window = item->window();
                        if (Q_UNLIKELY(!window) || (window->activeFocusItem() != item))
                            return;

                        // Confirms if the focus is lost:
                        if (item->isVisible() || !item->hasActiveFocus())
                            return;

                        const auto deduceNextItem = [item](bool forward, size_t limit = 2) {
                            assert(item);
                            auto nextItem = item->nextItemInFocusChain(forward);
                            size_t counter = 0;
                            while (nextItem && !nextItem->isVisible() && counter < limit)
                            {
                                nextItem = nextItem->nextItemInFocusChain(forward);
                                ++counter;
                            }
                            return nextItem;
                        };

                        auto nextItem = deduceNextItem(false);

                        if (!nextItem)
                            nextItem = deduceNextItem(true);

                        if (!nextItem)
                        {
                            qDebug() << "WindowFocusRecoveryManager: Could not deduce a sensible item to recover focus from" << item;
                            return;
                        }

                        bool ok;
                        // Qt Quick Control has `focusReason` property.
                        auto focusReason = item->property("focusReason").toInt(&ok);
#ifdef HAVE_QT_QUICK_PRIVATE
                        // TODO: Get rid of this when Qt moves `focusReason` property
                        //       from `QQuickControl` to `QQuickItem`.
                        if (!ok)
                        {
                            const auto itemPrivate = QQuickItemPrivate::get(item);
                            assert(itemPrivate);
                            focusReason = itemPrivate->focusReason;
                            ok = true;
                        }
#endif

                        if (ok)
                        {
                            const auto focusReasonEnum = static_cast<Qt::FocusReason>(focusReason);

                            qDebug() << "WindowFocusRecoveryManager: Recovering focus from" << item
                                     << "to" << nextItem
                                     << ", with focus reason:" << focusReasonEnum;

                            nextItem->forceActiveFocus(focusReasonEnum);
                        }
                        else
                        {
                            qDebug() << "WindowFocusRecoveryManager: Recovering focus from" << item
                                     << "to" << nextItem;

                            nextItem->forceActiveFocus();
                        }
                    }, Qt::QueuedConnection);

                });
            }
        }

        QQuickItem::itemChange(change, value);
    }

private:
    QPointer<QQuickWindow> m_window;
    QPointer<QQuickItem> m_activeFocusItem;
};

#endif // WINDOWFOCUSRECOVERYMANAGER_HPP
