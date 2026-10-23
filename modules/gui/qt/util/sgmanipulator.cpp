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
#include "sgmanipulator.hpp"

#include <QSGRenderNode>
#include <QSGGeometryNode>
#include <QSGTransformNode>
#include <QSGMaterial>

#include <QPointer>

#include <optional>
#include <unordered_map>
#include <tuple>
#include <memory>

class QSGMaterialProxy;

template<>
struct std::hash<std::tuple<QString, QString, QSGMaterialType*>>
{
    std::size_t operator()(const std::tuple<QString, QString, QSGMaterialType*> &key) const noexcept
    {
        return qHashMultiCommutative(0, std::get<0>(key), std::get<1>(key), std::get<2>(key));
    }
};

class QSGMaterialShaderProxy : public QSGMaterialShader
{
    QSGMaterialShader * const m_target;

public:
    // Owns the target:
    explicit QSGMaterialShaderProxy(QSGMaterialShader *target,
                                    const QString& vertexShader,
                                    const QString& fragmentShader)
        : m_target(target)
    {
        assert(target);

        // Unfortunately there is no `QSGMaterial::shaderFileName()`,
        // so we have to ask for both shaders in the proxy (even if
        // the shader is not overridden):
        assert(!vertexShader.isEmpty());
        assert(!fragmentShader.isEmpty());

        setShaderFileName(VertexStage, vertexShader);
        setShaderFileName(FragmentStage, fragmentShader);

        setFlag(UpdatesGraphicsPipelineState);
    }

    ~QSGMaterialShaderProxy()
    {
        delete m_target;
    }

    virtual bool updateUniformData(RenderState &state,
                                   QSGMaterial *newMaterial, QSGMaterial *oldMaterial) override;


    virtual void updateSampledImage(RenderState &state, int binding, QSGTexture **texture,
                                    QSGMaterial *newMaterial, QSGMaterial *oldMaterial) override;


    virtual bool updateGraphicsPipelineState(RenderState &state, GraphicsPipelineState *ps,
                                             QSGMaterial *newMaterial, QSGMaterial *oldMaterial) override;
};

class QSGMaterialProxy : public QSGMaterial
{
    QSGMaterial * const m_target;
    bool m_ownsTarget;

    QPointer<GraphicsPipelineState> m_graphicsPipelineState;
    QString m_vertexShader;
    QString m_fragmentShader;

    QSGMaterialType *m_type = nullptr;

    friend class QSGMaterialShaderProxy;
    friend class SGManipulator;

public:
    explicit QSGMaterialProxy(QSGMaterial *target, bool ownsTarget = true)
        : m_target(target)
        , m_ownsTarget(ownsTarget)
    {
        assert(target);
        setFlag(target->flags());
    }

    ~QSGMaterialProxy()
    {
        if (m_ownsTarget)
            delete m_target;
    }

    GraphicsPipelineState* graphicsPipelineState() const
    {
        return m_graphicsPipelineState;
    }

    void setGraphicsPipelineState(GraphicsPipelineState* state)
    {
        m_graphicsPipelineState = state;
    }

    void setShaderFileNames(const QString& vertexShader, const QString& fragmentShader)
    {
        m_vertexShader = vertexShader;
        m_fragmentShader = fragmentShader;

        // We can associate a type and cache it now:
        assert(m_target);

        assert(!m_fragmentShader.isEmpty());
        assert(!m_vertexShader.isEmpty());

        using KeyType = std::tuple<QString, QString, QSGMaterialType*>;

        static std::unordered_map<KeyType, std::unique_ptr<QSGMaterialType>> materialTypeMap;

        const KeyType key = { m_fragmentShader, m_vertexShader, m_target->type() };

        QSGMaterialType* ret;
        if (materialTypeMap.find(key) != materialTypeMap.end())
        {
            ret = materialTypeMap[key].get();
            assert(ret);
        }
        else
        {
            auto upRet = std::make_unique<QSGMaterialType>();
            ret = upRet.get();
            materialTypeMap[key] = std::move(upRet);
        }

        m_type = ret;
    }

    virtual QSGMaterialType *type() const override
    {
        assert(m_type);
        return m_type;
    }

    virtual QSGMaterialShader *createShader(QSGRendererInterface::RenderMode renderMode) const override
    {
        assert(m_target);
        return new QSGMaterialShaderProxy(m_target->createShader(renderMode), m_vertexShader, m_fragmentShader);
    }

    virtual int compare(const QSGMaterial *other) const override
    {
        assert(m_target);
        assert(other);

        if (Q_UNLIKELY(this == other))
            return 0;

        const auto otherProxy = static_cast<const QSGMaterialProxy*>(other);

        if (Q_UNLIKELY(m_target->type() != otherProxy->m_target->type()))
            return 1;

        const int ret = m_target->compare(otherProxy->m_target);
        if (ret)
            return ret; // Already different, we can early return.

        {
            if (m_vertexShader != otherProxy->m_vertexShader)
                return 1;

            if (m_fragmentShader != otherProxy->m_fragmentShader)
                return 1;

            if (m_graphicsPipelineState && otherProxy->m_graphicsPipelineState &&
                (m_graphicsPipelineState != otherProxy->m_graphicsPipelineState))
            {
                QMutexLocker lock(&m_graphicsPipelineState->m_mutex);
                QMutexLocker lockOther(&otherProxy->m_graphicsPipelineState->m_mutex);

                // GraphicsPipelineState comparison:
                // FIXME: Properly compare and also return (-1) instead of (1) at all times when applicable.

                if (m_graphicsPipelineState->m_blendEnable != otherProxy->m_graphicsPipelineState->m_blendEnable)
                    return 1;

                if (m_graphicsPipelineState->m_srcColor != otherProxy->m_graphicsPipelineState->m_srcColor)
                    return 1;

                if (m_graphicsPipelineState->m_dstColor != otherProxy->m_graphicsPipelineState->m_dstColor)
                    return 1;

                if (m_graphicsPipelineState->m_colorWrite != otherProxy->m_graphicsPipelineState->m_colorWrite)
                    return 1;

                if (m_graphicsPipelineState->m_blendConstant != otherProxy->m_graphicsPipelineState->m_blendConstant)
                    return 1;

                if (m_graphicsPipelineState->m_cullMode != otherProxy->m_graphicsPipelineState->m_cullMode)
                    return 1;

#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
                if (m_graphicsPipelineState->m_polygonMode != otherProxy->m_graphicsPipelineState->m_polygonMode)
                    return 1;
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
                if (m_graphicsPipelineState->m_separateBlendFactors != otherProxy->m_graphicsPipelineState->m_separateBlendFactors)
                    return 1;

                if (m_graphicsPipelineState->m_srcAlpha != otherProxy->m_graphicsPipelineState->m_srcAlpha)
                    return 1;

                if (m_graphicsPipelineState->m_dstAlpha != otherProxy->m_graphicsPipelineState->m_dstAlpha)
                    return 1;
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
                if (m_graphicsPipelineState->m_opColor != otherProxy->m_graphicsPipelineState->m_opColor)
                    return 1;

                if (m_graphicsPipelineState->m_opAlpha != otherProxy->m_graphicsPipelineState->m_opAlpha)
                    return 1;
#endif
            }
            else if (!m_graphicsPipelineState && otherProxy->m_graphicsPipelineState)
                return -1;
            else if (m_graphicsPipelineState && !otherProxy->m_graphicsPipelineState)
                return 1;
        }

        return 0;
    }
};

bool QSGMaterialShaderProxy::updateUniformData(RenderState &state,
                                               QSGMaterial *newMaterial, QSGMaterial *oldMaterial)
{
    assert(m_target);
    assert(newMaterial);
    return m_target->updateUniformData(state,
                                       static_cast<QSGMaterialProxy*>(newMaterial)->m_target,
                                       oldMaterial ? static_cast<QSGMaterialProxy*>(oldMaterial)->m_target : nullptr);
}

void QSGMaterialShaderProxy::updateSampledImage(RenderState &state, int binding, QSGTexture **texture,
                                                QSGMaterial *newMaterial, QSGMaterial *oldMaterial)
{
    assert(m_target);
    assert(newMaterial);
    m_target->updateSampledImage(state,
                                 binding,
                                 texture,
                                 static_cast<QSGMaterialProxy*>(newMaterial)->m_target,
                                 oldMaterial ? static_cast<QSGMaterialProxy*>(oldMaterial)->m_target : nullptr);
}

bool QSGMaterialShaderProxy::updateGraphicsPipelineState(RenderState &state, GraphicsPipelineState *ps,
                                                         QSGMaterial *newMaterial, QSGMaterial *oldMaterial)
{
    assert(m_target);
    assert(newMaterial);

    const auto newMaterialProxy = static_cast<QSGMaterialProxy*>(newMaterial);

    bool changeMade = false;

    if (m_target->flags() & UpdatesGraphicsPipelineState)
    {
        changeMade = m_target->updateGraphicsPipelineState(state,
                                                           ps,
                                                           newMaterialProxy->m_target,
                                                           oldMaterial ? static_cast<QSGMaterialProxy*>(oldMaterial)->m_target : nullptr);
    }

    {
        if (newMaterialProxy->m_graphicsPipelineState)
        {
            const auto& tPs = newMaterialProxy->m_graphicsPipelineState; // For convenience

            QMutexLocker lock(&tPs->m_mutex);

            if (tPs->m_blendEnable && (ps->blendEnable != *tPs->m_blendEnable))
            {
                ps->blendEnable = *tPs->m_blendEnable;
                changeMade = true;
            }

            if (tPs->m_srcColor && (ps->srcColor != static_cast<GraphicsPipelineState::BlendFactor>(*tPs->m_srcColor)))
            {
                ps->srcColor = static_cast<GraphicsPipelineState::BlendFactor>(*tPs->m_srcColor);
                changeMade = true;
            }

            if (tPs->m_dstColor && (ps->dstColor != static_cast<GraphicsPipelineState::BlendFactor>(*tPs->m_dstColor)))
            {
                ps->dstColor = static_cast<GraphicsPipelineState::BlendFactor>(*tPs->m_dstColor);
                changeMade = true;
            }

            if (tPs->m_colorWrite && (ps->colorWrite != GraphicsPipelineState::ColorMask::fromInt(tPs->m_colorWrite->toInt())))
            {
                ps->colorWrite = GraphicsPipelineState::ColorMask::fromInt(tPs->m_colorWrite->toInt());
                changeMade = true;
            }

            if (tPs->m_blendConstant && (ps->blendConstant != *tPs->m_blendConstant))
            {
                ps->blendConstant = *tPs->m_blendConstant;
                changeMade = true;
            }

            if (tPs->m_cullMode && (ps->cullMode != static_cast<GraphicsPipelineState::CullMode>(*tPs->m_cullMode)))
            {
                ps->cullMode = static_cast<GraphicsPipelineState::CullMode>(*tPs->m_cullMode);
                changeMade = true;
            }

#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
            if (tPs->m_polygonMode && (ps->polygonMode != static_cast<GraphicsPipelineState::PolygonMode>(*tPs->m_polygonMode)))
            {
                ps->polygonMode = static_cast<GraphicsPipelineState::PolygonMode>(*tPs->m_polygonMode);
                changeMade = true;
            }
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
            if (tPs->m_separateBlendFactors && (ps->separateBlendFactors != *tPs->m_separateBlendFactors))
            {
                ps->separateBlendFactors = *tPs->m_separateBlendFactors;
                changeMade = true;
            }

            if (tPs->m_srcAlpha && (ps->srcAlpha != static_cast<GraphicsPipelineState::BlendFactor>(*tPs->m_srcAlpha)))
            {
                ps->srcAlpha = static_cast<GraphicsPipelineState::BlendFactor>(*tPs->m_srcAlpha);
                changeMade = true;
            }

            if (tPs->m_dstAlpha && (ps->dstAlpha != static_cast<GraphicsPipelineState::BlendFactor>(*tPs->m_dstAlpha)))
            {
                ps->dstAlpha = static_cast<GraphicsPipelineState::BlendFactor>(*tPs->m_dstAlpha);
                changeMade = true;
            }
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
            if (tPs->m_opColor && (ps->opColor != static_cast<GraphicsPipelineState::BlendOp>(*tPs->m_opColor)))
            {
                ps->opColor = static_cast<GraphicsPipelineState::BlendOp>(*tPs->m_opColor);
                changeMade = true;
            }

            if (tPs->m_opAlpha && (ps->opAlpha != static_cast<GraphicsPipelineState::BlendOp>(*tPs->m_opAlpha)))
            {
                ps->opAlpha = static_cast<GraphicsPipelineState::BlendOp>(*tPs->m_opAlpha);
                changeMade = true;
            }
#endif
        }
    }

    return changeMade;
}


SGManipulator::SGManipulator(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(QQuickItem::ItemHasContents);

    connect(this, &SGManipulator::constantUpdateChanged, this, [this](bool constantUpdate) {
        if (constantUpdate)
            update();
    });

    connect(this, &SGManipulator::vertexShaderChanged, this, [this]() {
        m_shaderFileNameDirty = true;
        update();
    });

    connect(this, &SGManipulator::fragmentShaderChanged, this, [this]() {
        m_shaderFileNameDirty = true;
        update();
    });

    connect(this, &QQuickItem::visibleChanged, this, [this]() {
        update();
    });
}

QVariant SGManipulator::blending() const
{
    if (m_blending)
        return *m_blending;
    else
        return {};
}

void SGManipulator::setBlending(const QVariant& blending)
{
    assert(blending.typeId() == QMetaType::Bool);
    if (m_blending == blending.toBool())
        return;

    m_blending = blending.toBool();

    emit blendingChanged(blending);

    update();
}

void SGManipulator::resetBlending()
{
    if (!m_blending)
        return;

    m_blending.reset();

    emit blendingChanged({});

    update();
}

QSGNode *SGManipulator::updatePaintNode(QSGNode *, UpdatePaintNodeData *data)
{
    assert(data);

    const auto transformNode = data->transformNode;
    assert(transformNode);

    QSGNode *targetNode = nullptr;
    QSGGeometryNode *targetGeometryNode = nullptr;

    QSGNode *i = transformNode;
    while (i)
    {
        // NOTE: The target node must not be a transform node.
        // NOTE: The transform nodes parent sibling item's rendering nodes.
        const auto type = i->type();
        if (type == QSGNode::TransformNodeType)
        {
            i = i->previousSibling();
            continue;
        }

        targetNode = i;

        switch (type)
        {
        case QSGNode::GeometryNodeType:
            assert(dynamic_cast<QSGGeometryNode*>(i));
            targetGeometryNode = static_cast<QSGGeometryNode*>(i);
            break;
        default:
            targetGeometryNode = nullptr;
            break;
        }

        break;
    }

    const bool visible = isVisible();

    if (targetNode)
    {
        if (targetGeometryNode)
        {
            bool materialChangeMade = false;

            const auto material = targetGeometryNode->material();
            // WARNING: Qt explicitly sets the material pointer to 1 with software and OpenVG modes:
            if (material && material != reinterpret_cast<QSGMaterial*>(1))
            {
                if (visible && m_blending)
                {
                    if (*m_blending != material->flags().testFlag(QSGMaterial::Blending))
                    {
                        qDebug() << "SGManipulator: Blending is overridden as" << *m_blending << "for target material" << material;
                        material->setFlag(QSGMaterial::Blending, *m_blending);
                        materialChangeMade = true;
                    }
                }

                auto proxyMaterial = dynamic_cast<QSGMaterialProxy*>(material);

                if (visible && !m_fragmentShader.isEmpty() && !m_vertexShader.isEmpty())
                {
                    if (!proxyMaterial)
                    {
                        m_targetWasOwningMaterial = (targetGeometryNode->flags() & QSGNode::OwnsMaterial);
                        if (m_targetWasOwningMaterial)
                            targetGeometryNode->setFlag(QSGNode::OwnsMaterial, false); // To prevent destroying the target material

                        proxyMaterial = new QSGMaterialProxy(material, m_targetWasOwningMaterial);
                        proxyMaterial->setGraphicsPipelineState(m_ps);
                        proxyMaterial->setShaderFileNames(m_vertexShader,
                                                          m_fragmentShader);

                        targetGeometryNode->setMaterial(proxyMaterial);
                        targetGeometryNode->setOpaqueMaterial(nullptr);

                        targetGeometryNode->setFlag(QSGNode::OwnsMaterial, true); // Proxy material is always owned by the node to prevent leak.

                        materialChangeMade = true;
                    }
                    else
                    {
                        proxyMaterial = static_cast<QSGMaterialProxy*>(material);
                    }

                    if (m_psDirty)
                    {
                        proxyMaterial->setGraphicsPipelineState(m_ps);
                        m_psDirty = false;

                        materialChangeMade = true;
                    }

                    if (m_shaderFileNameDirty)
                    {
                        proxyMaterial->setShaderFileNames(m_vertexShader,
                                                          m_fragmentShader);
                        m_shaderFileNameDirty = false;

                        materialChangeMade = true;
                    }
                }
                else if (!visible && proxyMaterial)
                {
                    assert(targetGeometryNode->flags().testFlag(QSGNode::OwnsMaterial));
                    assert(proxyMaterial->m_target);

                    proxyMaterial->m_ownsTarget = false;
                    // The node will delete the proxy material since it owns the material,
                    // we don't need to delete ourselves:
                    targetGeometryNode->setMaterial(proxyMaterial->m_target); // Restore the old material

                    targetGeometryNode->setFlag(QSGNode::OwnsMaterial, m_targetWasOwningMaterial);

                    materialChangeMade = true;
                }

                if (materialChangeMade)
                    targetGeometryNode->markDirty(QSGNode::DirtyMaterial);
            }
        }
    }

    if (m_constantUpdate)
        update(); // re-schedule an update

    return nullptr;
}

void SGManipulator::releaseResources()
{
    QQuickItem::releaseResources();

    // This is called in the GUI thread.
    delete m_ps;
}
