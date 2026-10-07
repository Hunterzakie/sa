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
#include "selectableqmlpropertymap.hpp"

#include <QMenu>

#include "menus/menus.hpp"
#include "maininterface/mainctx.hpp"

SelectableQmlPropertyMap::SelectableQmlPropertyMap(QObject *parent)
    : QQmlPropertyMap(this, parent) // See the docs on why this constructor is used
{

}

void SelectableQmlPropertyMap::popupCheckableMenu(const QPoint &pos, MainCtx *mainCtx)
{
    const auto menu = new VLCMenu(mainCtx ? mainCtx->getIntf() : nullptr);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    // This is currently not guaranteed to be ordered, if you want it to be guaranteed,
    // you can iterate through the selection model instead:
    for (const auto& key : keys())
    {
        QString text;

        // Do not use const reference here, because it may bind to rvalue, leading to call the const operator that may copy the value:
        QVariant& value = (*this)[key];
        if (value.canConvert<QVariantMap>())
        {
            const auto valueMap = value.toMap();
            if (const auto it = valueMap.constFind(QStringLiteral("actionText")); it != valueMap.constEnd())
            {
                text = it.value().value<QString>();
            }
        }

        if (text.isEmpty())
            text = key;

        QAction *action = menu->addAction(text);
        assert(action);
        action->setCheckable(true);
        action->setChecked(m_selectionModel[key]);

        connect(action, &QAction::triggered, this, [this, key](bool checked) {
            select(key, checked ? Select : Deselect);
        });
    }

    menu->popup(pos);
}

bool SelectableQmlPropertyMap::hasSelection() const
{
    for (auto it = m_selectionModel.cbegin(); it != m_selectionModel.cend(); it++)
    {
        if (it.value())
            return true;
    }

    return false;
}

bool SelectableQmlPropertyMap::isSelected(const QString &key) const
{
    if (!contains(key) || !m_selectionModel.contains(key))
        return false;

    return m_selectionModel[key];
}

QList<QString> SelectableQmlPropertyMap::selectedItems() const
{
    if (m_cache && m_cachedSelection)
    {
        return *m_cachedSelection;
    }

    QList<QString> ret;
    ret.reserve(m_selectionModelList.size()); // Reserve for the worst case (all selected)

    assert(m_selectionModelList.size() == m_selectionModel.size());

    for (const auto &key : std::as_const(m_selectionModelList))
    {
        const auto it = m_selectionModel.constFind(key);
        assert(it != m_selectionModel.cend());
        if (it.value())
            ret.push_back(key);
    }

    if (m_cache)
    {
        m_cachedSelection = ret;
    }

    return ret;
}

void SelectableQmlPropertyMap::select(const QList<QString> &keys, SelectionFlag command)
{
    QList<QString> selectedKeys;
    QList<QString> deselectedKeys;

    // Worst case:
    selectedKeys.reserve(keys.size());
    deselectedKeys.reserve(keys.size());

    for (const auto& key : keys)
    {
        if (Q_UNLIKELY(!contains(key)))
            continue;

        assert(m_selectionModel.contains(key));

        bool& selected = m_selectionModel[key];

        switch (command)
        {
        case NoUpdate:
            break; // no-op
        case Select:
        {
            if (selected)
                break;

            selected = true;

            selectedKeys.push_back(key);

            break;
        }
        case Clear:
        case Deselect:
        {
            if (!selected)
                break;

            selected = false;

            deselectedKeys.push_back(key);

            break;
        }
        case Toggle:
        {
            selected = !selected;

            if (selected)
                selectedKeys.push_back(key);
            else
                deselectedKeys.push_back(key);

            break;
        }
        default:
            break;
        }
    }

    m_cachedSelection.reset(); // invalidate cache

    emit selectionChanged(selectedKeys, deselectedKeys);
}

bool SelectableQmlPropertyMap::select(const QString &key, SelectableQmlPropertyMap::SelectionFlag command)
{
    if (Q_UNLIKELY(!contains(key)))
        return false;

    assert(m_selectionModel.contains(key));

    bool& selected = m_selectionModel[key];

    switch (command)
    {
    case NoUpdate:
        return true; // no-op, does not fail
    case Select:
    {
        if (selected)
            return false;

        selected = true;

        m_cachedSelection.reset(); // invalidate cache

        emit selectionChanged({{key}}, {});

        break;
    }
    case Clear:
    case Deselect:
    {
        if (!selected)
            return false;

        selected = false;

        m_cachedSelection.reset(); // invalidate cache

        emit selectionChanged({}, {{key}});

        break;
    }
    case Toggle:
    {
        selected = !selected;

        m_cachedSelection.reset(); // invalidate cache

        if (selected)
            emit selectionChanged({{key}}, {});
        else
            emit selectionChanged({}, {{key}});

        break;
    }
    default:
        break;
    }

    return true;
}

void SelectableQmlPropertyMap::clear()
{
    for (auto it = m_selectionModel.begin(); it != m_selectionModel.end(); it++)
    {
        it.value() = false;
    }
}

void SelectableQmlPropertyMap::reset()
{
    QList<QString> ret;
    ret.reserve(m_selectionModel.size()); // Reserve for the worst case (all selected)

    for (auto it = m_selectionModel.begin(); it != m_selectionModel.end(); it++)
    {
        if (!it.value())
            continue; // already not selected

        ret.push_back(it.key());
        it.value() = false;
    }

    m_cachedSelection.reset(); // invalidate cache

    emit selectionChanged({}, ret);
}

QVariant SelectableQmlPropertyMap::updateValue(const QString &key, const QVariant &input)
{
    if (!m_selectionModel.contains(key))
    {
        m_selectionModel.insert(key, false);
        m_selectionModelList.push_back(key);
    }

    return QQmlPropertyMap::updateValue(key, input);
}
