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
#ifndef VIEWPORTOBSERVER_HPP
#define VIEWPORTOBSERVER_HPP

#include <QQuickItem>
#include <QPointer>

// This utility class observes a viewport (such as `Flickable`, or an indirect
// parent that has `clip` set), and generically prevents overdraw to improve rendering
// performance. You do not need to use this class, if the target is an item that
// already observes the viewport and prevents overdraw, such as `Text`. You also
// do not need to use this class in views, where partial loading is applicable.
// One ideal use case of this class would be when you have a scrollable `Flickable`
// with complex sub-items. The target item is currently not adjustable and is the
// parent item (that we temporarily* override its opacity to 0.0 if completely
// outside the viewport). Note that in most cases this item should have the same
// size as the target item, and be placed in position (0.0, 0.0), which means you
// can simply use `anchors.fill: parent`, but it can also be different -if for
// some reason- you want to configure the area of interest (which is relevant
// if you want to have display margins, for example).
// NOTE: You can use `QSG_VISUALIZE=overdraw` to see where this class would be
//       useful.
// WARNING: There is a reason Qt does not do this by default itself. Frustum
//          culling can be expensive if it is done for many items. Only use
//          overdraw prevention where it makes sense, such as complex sub-view
//          roots, and never in delegate instances where the model can be big.
// (*) Temporarily means that once the target goes back into the viewport, the
//     original binding value is restored. This means that you can continue to
//     have your own setting/binding for `opacity`/`visible`.
class ViewportObserver : public QQuickItem
{
    Q_OBJECT

    // See `preferAdjustingVisibilityOverOpacity` to configure how to prevent overdraw:
    Q_PROPERTY(bool preventOverdraw MEMBER m_preventOverdraw NOTIFY preventOverdrawChanged FINAL)

    // Adjusting visibility is a more extreme optimization, and may cause breakage
    // if layouting depends on visibility (which is often the case). When applicable,
    // it allows getting rid of the scene graph nodes altogether, which are to be
    // regenerated when the target item gets into the viewport again. In some cases,
    // this may allow releasing graphics resources as well. Note that optimization
    // by adjusting opacity to 0.0 relies on the fact that the default scene graph
    // renderer prevents re-draw if opacity is 0.0, and it will not be applicable
    // if you use a custom renderer that does not behave as such.
    // The default value is false (opacity is adjusted instead). Do not set this
    // property unless you really need to release the scene graph nodes, rather
    // than just preventing them being rendered. This property is only applicable
    // when `preventOverdraw` is set.
    Q_PROPERTY(bool preferAdjustingVisibilityOverOpacity READ preferAdjustingVisibilityOverOpacity
                                                         WRITE setPreferAdjustingVisibilityOverOpacity
                                                         NOTIFY preferAdjustingVisibilityOverOpacityChanged
                                                         FINAL)

    // For convenience, since `QQuickItem::clipRect()` is not accessible in QML.
    // NOTE: Get rid of this if Qt introduces such property itself.
    Q_PROPERTY(QRectF clipRect READ clipRect NOTIFY clipRectChanged FINAL)

    QML_ELEMENT

public:
    explicit ViewportObserver(QQuickItem *parent = nullptr);

    bool preferAdjustingVisibilityOverOpacity() const;
    void setPreferAdjustingVisibilityOverOpacity(bool option);

signals:
    void preferAdjustingVisibilityOverOpacityChanged(bool);
    void clipRectChanged(const QRectF&);
    void preventOverdrawChanged(bool);

protected:
    void componentComplete() override;

    void itemChange(ItemChange, const ItemChangeData &) override;

    void onTransformChanged();

private:
    QPointer<QQmlComponent> m_qmlBindingFactory;

    QPointer<QObject> m_qmlOpacityBinding;
    QPointer<QObject> m_qmlVisibleBinding;
    QPointer<QObject> m_activeBinding;

    QRectF m_oldClipRect;

    bool m_preventOverdraw = true; // Default true (currently the sole purpose of this class)
};

#endif // VIEWPORTOBSERVER_HPP
