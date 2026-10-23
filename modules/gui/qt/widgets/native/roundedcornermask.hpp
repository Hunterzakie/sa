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

#ifndef ROUNDEDCORNERMASK_HPP
#define ROUNDEDCORNERMASK_HPP

#include <QQuickItem>
#include <QColor>

// This item replaces the area outside of a rounded corner arc with `color`,
// akin to `ViewBlockingRectangle` (the color is written as is, including its
// alpha, it is not blended over what is behind), but with anti-aliasing at the
// arc edge. This allows rounding the corner of arbitrary content without
// rendering it into an intermediate texture, as long as this item is rendered
// after (above) the content.
//
// The arc center is placed at `radius` from the `corner` of the item, in both
// directions. The item can be bigger than `radius`, the area that is not
// in the corner quadrant is left untouched.
//
// This item requires a RHI based scene graph adaptation, nothing is rendered
// otherwise (see `supported`).
class RoundedCornerMask : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged FINAL)
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged FINAL)
    Q_PROPERTY(Qt::Corner corner READ corner WRITE setCorner NOTIFY cornerChanged FINAL)

    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged FINAL)

public:
    explicit RoundedCornerMask(QQuickItem *parent = nullptr);

    QColor color() const { return m_color; }
    void setColor(const QColor& color);

    qreal radius() const { return m_radius; }
    void setRadius(qreal radius);

    Qt::Corner corner() const { return m_corner; }
    void setCorner(Qt::Corner corner);

    bool supported() const { return m_supported; }

protected:
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *) override;

private:
    void updateSupported();

    QColor m_color {Qt::transparent};
    qreal m_radius = 0.0;
    Qt::Corner m_corner = Qt::TopLeftCorner;
    bool m_supported = false;

signals:
    void colorChanged();
    void radiusChanged();
    void cornerChanged();
    void supportedChanged();
};

#endif
