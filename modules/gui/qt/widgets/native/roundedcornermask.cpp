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

#include "roundedcornermask.hpp"

#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGRendererInterface>

namespace {

class RoundedCornerMaskMaterial : public QSGMaterial
{
public:
    RoundedCornerMaskMaterial()
    {
        // This puts the node in the alpha pass, which is rendered after the
        // opaque pass and in back to front order. This is necessary so that
        // the content below is already rendered when this node is rendered:
        setFlag(QSGMaterial::Blending);
    }

    QSGMaterialType *type() const override
    {
        static QSGMaterialType type;
        return &type;
    }

    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;

    int compare(const QSGMaterial *other) const override
    {
        const auto o = static_cast<const RoundedCornerMaskMaterial *>(other);

        if (m_radius != o->m_radius)
            return (m_radius < o->m_radius) ? -1 : 1;

        // The color is part of the pipeline state (blend constant):
        const QRgb a = m_color.rgba();
        const QRgb b = o->m_color.rgba();
        if (a != b)
            return (a < b) ? -1 : 1;

        return 0;
    }

    QColor m_color {Qt::transparent};
    float m_radius = 0.f;
};

class RoundedCornerMaskMaterialShader : public QSGMaterialShader
{
public:
    RoundedCornerMaskMaterialShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/shaders/RoundedCornerMask.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/shaders/RoundedCornerMask.frag.qsb"));
        setFlag(UpdatesGraphicsPipelineState);
    }

    bool updateUniformData(RenderState &state, QSGMaterial *newMaterial, QSGMaterial *oldMaterial) override
    {
        const auto material = static_cast<RoundedCornerMaskMaterial *>(newMaterial);
        const auto old = static_cast<RoundedCornerMaskMaterial *>(oldMaterial);

        QByteArray *buf = state.uniformData();
        assert(buf->size() >= 72);

        bool changed = false;

        if (state.isMatrixDirty())
        {
            const QMatrix4x4 m = state.combinedMatrix();
            memcpy(buf->data(), m.constData(), 64);
            changed = true;
        }

        if (state.isOpacityDirty())
        {
            const float opacity = state.opacity();
            memcpy(buf->data() + 64, &opacity, 4);
            changed = true;
        }

        if (!old || old->m_radius != material->m_radius)
        {
            memcpy(buf->data() + 68, &material->m_radius, 4);
            changed = true;
        }

        return changed;
    }

    bool updateGraphicsPipelineState(RenderState &, GraphicsPipelineState *ps,
                                     QSGMaterial *newMaterial, QSGMaterial *) override
    {
        const auto material = static_cast<RoundedCornerMaskMaterial *>(newMaterial);

        // `dst = constant * src + dst * (1 - src)`, where the fragment shader
        // outputs the same value (outside arc coverage) in all the channels.
        // Using the same factors for the alpha channel is intended:
        ps->blendEnable = true;
        ps->srcColor = GraphicsPipelineState::ConstantColor;
        ps->dstColor = GraphicsPipelineState::OneMinusSrcColor;

        const QColor color = material->m_color.toRgb();
        const float alpha = color.alphaF();
        ps->blendConstant = QColor::fromRgbF(color.redF() * alpha,
                                             color.greenF() * alpha,
                                             color.blueF() * alpha,
                                             alpha);
        return true;
    }
};

QSGMaterialShader *RoundedCornerMaskMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new RoundedCornerMaskMaterialShader;
}

} // anonymous namespace

RoundedCornerMask::RoundedCornerMask(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(QQuickItem::ItemHasContents);
    connect(this, &QQuickItem::windowChanged, this, &RoundedCornerMask::updateSupported);
}

void RoundedCornerMask::setColor(const QColor& color)
{
    if (m_color == color)
        return;
    m_color = color;
    emit colorChanged();
    update();
}

void RoundedCornerMask::setRadius(qreal radius)
{
    if (qFuzzyCompare(m_radius, radius))
        return;
    m_radius = radius;
    emit radiusChanged();
    update();
}

void RoundedCornerMask::setCorner(Qt::Corner corner)
{
    if (m_corner == corner)
        return;
    m_corner = corner;
    emit cornerChanged();
    update();
}

void RoundedCornerMask::updateSupported()
{
    const QQuickWindow* const w = window();
    const bool supported = w && w->rendererInterface() &&
                           QSGRendererInterface::isApiRhiBased(w->rendererInterface()->graphicsApi());

    if (m_supported == supported)
        return;
    m_supported = supported;
    emit supportedChanged();
}

QSGNode *RoundedCornerMask::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto node = static_cast<QSGGeometryNode *>(oldNode);

    const QRectF rect = boundingRect();

    if (!m_supported || rect.isEmpty() || m_radius <= 0.0)
    {
        delete node;
        return nullptr;
    }

    if (!node)
    {
        node = new QSGGeometryNode;

        auto geometry = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4);
        geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);

        node->setMaterial(new RoundedCornerMaskMaterial);
        node->setFlag(QSGNode::OwnsMaterial);
    }

    // The texture coordinates are used for the position relative to the arc
    // center, mirrored such that the corner quadrant is always the negative one:
    const bool left = (m_corner == Qt::TopLeftCorner || m_corner == Qt::BottomLeftCorner);
    const bool top = (m_corner == Qt::TopLeftCorner || m_corner == Qt::TopRightCorner);

    const qreal centerX = left ? (rect.left() + m_radius) : (rect.right() - m_radius);
    const qreal centerY = top ? (rect.top() + m_radius) : (rect.bottom() - m_radius);

    const auto coord = [&](qreal x, qreal y) {
        return QPointF(left ? (x - centerX) : (centerX - x),
                       top ? (y - centerY) : (centerY - y));
    };

    QSGGeometry::TexturedPoint2D *v = node->geometry()->vertexDataAsTexturedPoint2D();
    const auto set = [&](int i, qreal x, qreal y) {
        const QPointF c = coord(x, y);
        v[i].set(x, y, c.x(), c.y());
    };
    set(0, rect.left(), rect.top());
    set(1, rect.right(), rect.top());
    set(2, rect.left(), rect.bottom());
    set(3, rect.right(), rect.bottom());
    node->markDirty(QSGNode::DirtyGeometry);

    const auto material = static_cast<RoundedCornerMaskMaterial *>(node->material());
    const float radius = m_radius;
    if (material->m_color != m_color || material->m_radius != radius)
    {
        material->m_color = m_color;
        material->m_radius = radius;
        node->markDirty(QSGNode::DirtyMaterial);
    }

    return node;
}
