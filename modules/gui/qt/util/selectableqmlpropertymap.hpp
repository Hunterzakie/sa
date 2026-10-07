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
#ifndef SELECTABLEQMLPROPERTYMAP_HPP
#define SELECTABLEQMLPROPERTYMAP_HPP

#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QHash>
#include <QString>

#include <optional>

Q_MOC_INCLUDE("maininterface/mainctx.hpp") // For `popupCheckableMenu(..., MainCtx)`

class SelectableQmlPropertyMap : public QQmlPropertyMap
{
    Q_OBJECT

    // It is guaranteed that the selected items is ordered with respect to the insertion order.
    Q_PROPERTY(QList<QString> selectedItems READ selectedItems NOTIFY selectionChanged FINAL)

    // Set `cache` if you intend to read `selectedItems` many times,
    // such can be the case if it is used in a QML binding, for example
    // `selected: selectablePropertyMap.includes(key)`.
    // Also see `ListSelectionModel`.
    // By default, it is true.
    Q_PROPERTY(bool cache MEMBER m_cache NOTIFY cacheChanged FINAL)

    QML_ELEMENT

public:
    // Akin to `QItemSelectionModel`:
    enum SelectionFlag
    {
        NoUpdate       = 0x0000,
        Clear          = 0x0001,
        Select         = 0x0002,
        Deselect       = 0x0004,
        Toggle         = 0x0008
    };
    Q_ENUM(SelectionFlag);

    explicit SelectableQmlPropertyMap(QObject *parent = nullptr);

    // It is guaranteed that the selected items is ordered with respect to the insertion order.
    // This is not an invokable slot, because we expose a property for it:
    QList<QString> selectedItems() const;

    // Re-defined to make it invokable:
    Q_INVOKABLE void freeze()
    {
        QQmlPropertyMap::freeze();
    }

    Q_INVOKABLE bool contains(const QString &key) const
    {
        return QQmlPropertyMap::contains(key);
    }

signals:
    void cacheChanged(bool);

    // Akin to `QItemSelectionModel`:
    void selectionChanged(const QList<QString>& selected, const QList<QString>& deselected);

public slots:
    // Opens convenience menu with checkable actions:
    // If the item provides "actionText", it is used instead of the key as menu item.
    virtual void popupCheckableMenu(const QPoint& pos, class MainCtx* mainCtx);

    // Akin to `QItemSelectionModel`:

    bool hasSelection() const;
    bool isSelected(const QString& key) const;
    // If the key does not exist, `select()` is not going to be functional (but it will not cause assertion failure):
    virtual void select(const QList<QString>& keys, SelectableQmlPropertyMap::SelectionFlag command);
    virtual bool select(const QString& key, SelectableQmlPropertyMap::SelectionFlag command);
    // Clears and emits `QQmlPropertyMap::valueChanged()` signal:
    virtual void clear();
    // Clears without emitting a signal:
    virtual void reset();

protected:
    QVariant updateValue(const QString &key, const QVariant &input) override;

private:
    QHash<QString, bool> m_selectionModel;
    // Since both `QHash` and `QMap` is not ordered with respect to
    // insertion order, we need to keep track of the insertion order.
    // This is not ideal, but should be totally okay because `QString`
    // is implicitly shared (copy on write):
    QList<QString> m_selectionModelList;

    bool m_cache = true;

    mutable std::optional<QList<QString>> m_cachedSelection;
};

#endif // SELECTABLEQMLPROPERTYMAP_HPP
