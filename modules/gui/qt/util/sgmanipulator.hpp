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
#ifndef SGMANIPULATOR_H
#define SGMANIPULATOR_H

#include <QQuickItem>
#include <QVariant>
#include <QMutex>

#include <optional>

// TODO: Investigate whether to make this type a value type:
class GraphicsPipelineState : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariant blendEnable READ blendEnable WRITE setBlendEnable RESET resetBlendEnable NOTIFY adjusted FINAL)
    Q_PROPERTY(QVariant srcColor READ srcColor WRITE setSrcColor RESET resetSrcColor NOTIFY adjusted FINAL)
    Q_PROPERTY(QVariant dstColor READ dstColor WRITE setDstColor RESET resetDstColor NOTIFY adjusted FINAL)
    Q_PROPERTY(QVariant colorWrite READ colorWrite WRITE setColorWrite RESET resetColorWrite NOTIFY adjusted FINAL)
    Q_PROPERTY(QVariant blendConstant READ blendConstant WRITE setBlendConstant RESET resetBlendConstant NOTIFY adjusted FINAL)
    Q_PROPERTY(QVariant cullMode READ cullMode WRITE setCullMode RESET resetCullMode NOTIFY adjusted FINAL)
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    Q_PROPERTY(QVariant polygonMode READ polygonMode WRITE setPolygonMode RESET resetPolygonMode NOTIFY adjusted FINAL)
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    Q_PROPERTY(QVariant separateBlendFactors READ separateBlendFactors WRITE setSeparateBlendFactors RESET resetSeparateBlendFactors NOTIFY adjusted FINAL)
    Q_PROPERTY(QVariant srcAlpha READ srcAlpha WRITE setSrcAlpha RESET resetSrcAlpha NOTIFY adjusted FINAL)
    Q_PROPERTY(QVariant dstAlpha READ dstAlpha WRITE setDstAlpha RESET resetDstAlpha NOTIFY adjusted FINAL)
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    Q_PROPERTY(QVariant opColor READ opColor WRITE setOpColor RESET resetOpColor NOTIFY adjusted FINAL)
    Q_PROPERTY(QVariant opAlpha READ opAlpha WRITE setOpAlpha RESET resetOpAlpha NOTIFY adjusted FINAL)
#endif

    QML_ELEMENT
    QML_UNCREATABLE("GraphicsPipelineState is uncreatable.")

    friend class QSGMaterialProxy;
    friend class QSGMaterialShaderProxy;

public:
    explicit GraphicsPipelineState(QObject *parent = nullptr)
        : QObject(parent)
    { }

    // See `QSGMaterialShader`:

    enum BlendFactor {
        Zero,
        One,
        SrcColor,
        OneMinusSrcColor,
        DstColor,
        OneMinusDstColor,
        SrcAlpha,
        OneMinusSrcAlpha,
        DstAlpha,
        OneMinusDstAlpha,
        ConstantColor,
        OneMinusConstantColor,
        ConstantAlpha,
        OneMinusConstantAlpha,
        SrcAlphaSaturate,
        Src1Color,
        OneMinusSrc1Color,
        Src1Alpha,
        OneMinusSrc1Alpha
    };
    Q_ENUM(BlendFactor)

    enum class BlendOp {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max,
    };
    Q_ENUM(BlendOp)

    enum ColorMaskComponent {
        R = 1 << 0,
        G = 1 << 1,
        B = 1 << 2,
        A = 1 << 3
    };
    Q_FLAG(ColorMaskComponent)
    Q_DECLARE_FLAGS(ColorMask, ColorMaskComponent)

    enum CullMode {
        CullNone,
        CullFront,
        CullBack
    };
    Q_ENUM(CullMode)

#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    enum PolygonMode {
        Fill,
        Line,
    };
    Q_ENUM(PolygonMode)
#endif

    QVariant blendEnable() const
    {
        QMutexLocker lock(&m_mutex);
        if (m_blendEnable)
            return *m_blendEnable;
        else
            return {};
    }

    void setBlendEnable(const QVariant& blendEnable)
    {
        {
            QMutexLocker lock(&m_mutex);
            m_blendEnable = blendEnable.toBool();
        }
        emit adjusted();
    }

    void resetBlendEnable()
    {
        {
            QMutexLocker lock(&m_mutex);
            m_blendEnable.reset();
        }
        emit adjusted();
    }

    QVariant srcColor() const
    {
        QMutexLocker lock(&m_mutex);
        if (m_srcColor)
            return *m_srcColor;
        else
            return {};
    }

    void setSrcColor(const QVariant& srcColor)
    {
        {
            QMutexLocker lock(&m_mutex);
            m_srcColor = srcColor.value<BlendFactor>();
        }
        emit adjusted();
    }

    void resetSrcColor()
    {
        {
            QMutexLocker lock(&m_mutex);
            m_srcColor.reset();
        }
        emit adjusted();
    }

    QVariant dstColor() const
    {
        QMutexLocker lock(&m_mutex);
        if (m_dstColor)
            return *m_dstColor;
        else
            return {};
    }

    void setDstColor(const QVariant& dstColor)
    {
        {
            QMutexLocker lock(&m_mutex);
            m_dstColor = dstColor.value<BlendFactor>();
        }
        emit adjusted();
    }

    void resetDstColor()
    {
        {
            QMutexLocker lock(&m_mutex);
            m_dstColor.reset();
        }
        emit adjusted();
    }

    QVariant colorWrite() const
    {
        QMutexLocker lock(&m_mutex);
        if (m_colorWrite)
            return QVariant::fromValue(*m_colorWrite);
        else
            return {};
    }

    void setColorWrite(const QVariant& colorWrite)
    {
        {
            QMutexLocker lock(&m_mutex);
            m_colorWrite = colorWrite.value<ColorMask>();
        }
        emit adjusted();
    }

    void resetColorWrite()
    {
        {
            QMutexLocker lock(&m_mutex);
            m_colorWrite.reset();
        }
        emit adjusted();
    }

    QVariant blendConstant() const
    {
        QMutexLocker lock(&m_mutex);
        if (m_blendConstant)
            return *m_blendConstant;
        else
            return {};
    }

    void setBlendConstant(const QVariant& blendConstant)
    {
        {
            QMutexLocker lock(&m_mutex);
            m_blendConstant = blendConstant.value<QColor>();
        }
        emit adjusted();
    }

    void resetBlendConstant()
    {
        {
            QMutexLocker lock(&m_mutex);
            m_blendConstant.reset();
        }
        emit adjusted();
    }

    QVariant cullMode() const
    {
        QMutexLocker lock(&m_mutex);
        if (m_cullMode)
            return QVariant::fromValue(*m_cullMode);
        else
            return {};
    }

    void setCullMode(const QVariant& cullMode)
    {
        {
            QMutexLocker lock(&m_mutex);
            m_cullMode = cullMode.value<CullMode>();
        }
        emit adjusted();
    }

    void resetCullMode()
    {
        {
            QMutexLocker lock(&m_mutex);
            m_cullMode.reset();
        }
        emit adjusted();
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    QVariant polygonMode() const
    {
        QMutexLocker lock(&m_mutex);
        if (m_polygonMode)
            return QVariant::fromValue(*m_polygonMode);
        else
            return {};
    }

    void setPolygonMode(const QVariant& polygonMode)
    {
        {
            QMutexLocker lock(&m_mutex);
            m_polygonMode = polygonMode.value<PolygonMode>();
        }
        emit adjusted();
    }

    void resetPolygonMode()
    {
        {
            QMutexLocker lock(&m_mutex);
            m_polygonMode.reset();
        }
        emit adjusted();
    }
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QVariant separateBlendFactors() const
    {
        QMutexLocker lock(&m_mutex);
        if (m_separateBlendFactors)
            return *m_separateBlendFactors;
        else
            return {};
    }

    void setSeparateBlendFactors(const QVariant& separateBlendFactors)
    {
        {
            QMutexLocker lock(&m_mutex);
            m_separateBlendFactors = separateBlendFactors.toBool();
        }
        emit adjusted();
    }

    void resetSeparateBlendFactors()
    {
        {
            QMutexLocker lock(&m_mutex);
            m_separateBlendFactors.reset();
        }
        emit adjusted();
    }

    QVariant srcAlpha() const
    {
        QMutexLocker lock(&m_mutex);
        if (m_srcAlpha)
            return *m_srcAlpha;
        else
            return {};
    }

    void setSrcAlpha(const QVariant& srcAlpha)
    {
        {
            QMutexLocker lock(&m_mutex);
            m_srcAlpha = srcAlpha.value<BlendFactor>();
        }
        emit adjusted();
    }

    void resetSrcAlpha()
    {
        {
            QMutexLocker lock(&m_mutex);
            m_srcAlpha.reset();
        }
        emit adjusted();
    }

    QVariant dstAlpha() const
    {
        QMutexLocker lock(&m_mutex);
        if (m_dstAlpha)
            return *m_dstAlpha;
        else
            return {};
    }

    void setDstAlpha(const QVariant& dstAlpha)
    {
        {
            QMutexLocker lock(&m_mutex);
            m_dstAlpha = dstAlpha.value<BlendFactor>();
        }
        emit adjusted();
    }

    void resetDstAlpha()
    {
        {
            QMutexLocker lock(&m_mutex);
            m_dstAlpha.reset();
        }
        emit adjusted();
    }
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QVariant opColor() const
    {
        QMutexLocker lock(&m_mutex);
        if (m_opColor)
            return QVariant::fromValue(*m_opColor);
        else
            return {};
    }

    void setOpColor(const QVariant& opColor)
    {
        {
            QMutexLocker lock(&m_mutex);
            m_opColor = opColor.value<BlendOp>();
        }
        emit adjusted();
    }

    void resetOpColor()
    {
        {
            QMutexLocker lock(&m_mutex);
            m_opColor.reset();
        }
        emit adjusted();
    }

    QVariant opAlpha() const
    {
        QMutexLocker lock(&m_mutex);
        if (m_opAlpha)
            return QVariant::fromValue(*m_opAlpha);
        else
            return {};
    }

    void setOpAlpha(const QVariant& opAlpha)
    {
        {
            QMutexLocker lock(&m_mutex);
            m_opAlpha = opAlpha.value<BlendOp>();
        }
        emit adjusted();
    }

    void resetOpAlpha()
    {
        {
            QMutexLocker lock(&m_mutex);
            m_opAlpha.reset();
        }
        emit adjusted();
    }
#endif

signals:
    void adjusted();

private:
    mutable QMutex m_mutex;

    std::optional<bool> m_blendEnable;
    std::optional<BlendFactor> m_srcColor;
    std::optional<BlendFactor> m_dstColor;
    std::optional<ColorMask> m_colorWrite;
    std::optional<QColor> m_blendConstant;
    std::optional<CullMode> m_cullMode;
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    std::optional<PolygonMode> m_polygonMode;
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    std::optional<bool> m_separateBlendFactors;
    std::optional<BlendFactor> m_srcAlpha;
    std::optional<BlendFactor> m_dstAlpha;
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    std::optional<BlendOp> m_opColor;
    std::optional<BlendOp> m_opAlpha;
#endif
};

// This class allows manipulating the parent item's scene graph node.
// Visibility can be set to to false to disable manipulation.
// It is the user's responsibility to call `QQuickItem::update()`
// to update the settings. Currently an update is scheduled as soon
// as a manipulating property is changed. This means that if the
// target node is altered without the control of this class, an
// update should be scheduled (unless `constantUpdate` is set),
// this can be for example `Image` switching from a opaque image
// to a translucent one, or `Rectangle` switching from a opaque
// color to a transparent one. The same applies when the target
// node itself changes (such as due to re-parenting).
class SGManipulator : public QQuickItem
{
    Q_OBJECT

    // This is not really expensive, but still avoid constant updates if possible:
    Q_PROPERTY(bool constantUpdate MEMBER m_constantUpdate NOTIFY constantUpdateChanged FINAL)

    Q_PROPERTY(QVariant blending READ blending WRITE setBlending RESET resetBlending NOTIFY blendingChanged FINAL)

    // You can adjust the graphics pipeline state through `ps`,
    // but note that doing that breaks batch rendering.
    Q_PROPERTY(GraphicsPipelineState* ps READ ps CONSTANT FINAL)

    // These must be set for ps to be respected:
    Q_PROPERTY(QString vertexShader MEMBER m_vertexShader NOTIFY vertexShaderChanged FINAL)
    Q_PROPERTY(QString fragmentShader MEMBER m_fragmentShader NOTIFY fragmentShaderChanged FINAL)

public:
    explicit SGManipulator(QQuickItem* parent = nullptr);

    QVariant blending() const;
    void setBlending(const QVariant& blending);
    void resetBlending();

    GraphicsPipelineState* ps()
    {
        if (!m_ps)
        {
            m_ps = new GraphicsPipelineState;

            connect(m_ps, &GraphicsPipelineState::adjusted, this, [this]() {
                m_psDirty = true;
                update();
            });
        }

        return m_ps;
    }

signals:
    void blendingChanged(const QVariant&);
    void constantUpdateChanged(bool);

    void vertexShaderChanged();
    void fragmentShaderChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *) override;

    void releaseResources() override;

private:
    std::optional<bool> m_blending;
    bool m_constantUpdate = false;
    mutable QPointer<GraphicsPipelineState> m_ps;
    QString m_vertexShader;
    QString m_fragmentShader;
    bool m_targetWasOwningMaterial = false;

    bool m_shaderFileNameDirty = false;
    bool m_psDirty = false;
};

#endif // SGMANIPULATOR_H
