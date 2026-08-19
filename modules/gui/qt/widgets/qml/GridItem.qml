/*****************************************************************************
 * Copyright (C) 2019 VLC authors and VideoLAN
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
import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import QtQml.Models

import VLC.MainInterface
import VLC.Widgets as Widgets
import VLC.Util
import VLC.Style

T.Control {
    id: root

    // Properties

    readonly property GridView view: GridView.view

    required property int index

    property real pictureWidth: VLCStyle.colWidth(1)
    property real pictureHeight: pictureWidth
    property int titleTopMargin: VLCStyle.gridItemTitle_topMargin
    property int subtitleTopMargin: VLCStyle.gridItemSubtitle_topMargin
    property Item dragItem: null

    readonly property int selectedBorderWidth: VLCStyle.gridItemSelectedBorder

    property int _modifiersOnLastPress: Qt.NoModifier

    // if true, texts are horizontally centered, provided it can fit in pictureWidth
    property bool textAlignHCenter: false

    // if the item is selected
    readonly property bool selected: view.selectionModel.selectedIndexesFlat.includes(index)

    hoverEnabled: false // We handle through the hover handler which considers the view spacing

    readonly property bool effectiveHovered: contentItem?.hovered || (hoverEnabled && hovered)

    GridView.delayRemove: dragHandler.active

    // Aliases

    property alias mediaCover: picture

    property alias image: picture.source
    property alias cacheImage: picture.cacheImage
    property alias fallbackImage: picture.fallbackImageSource

    property alias fillMode: picture.fillMode

    property alias title: titleLabel.text
    property alias subtitle: subtitleTxt.text
    property alias subtitleVisible: subtitleTxt.visible
    property alias playCoverShowPlay: picture.playCoverShowPlay
    property alias implicitPlayCoverShowPlay: picture.implicitPlayCoverShowPlay
    property alias playIconSize: picture.playIconSize
    property alias pictureRadius: picture.radius
    property alias effectiveRadius: picture.effectiveRadius
    property alias pictureOverlay: picture.imageOverlay

    property alias selectedShadow: selectedShadow
    property alias unselectedShadow: unselectedShadow

    property alias artworkTextureProvider: picture.textureProvider

    // Signals

    signal playClicked
    signal addToPlaylistClicked
    signal itemClicked(int modifier, bool select)
    signal itemDoubleClicked(int modifier)
    signal contextMenuButtonClicked(Item menuParent, point globalMousePos)

    // Settings

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)


    property real preferredLeftInset
    property real preferredRightInset
    property real preferredTopInset
    property real preferredBottomInset

    property real preferredLeftPadding: VLCStyle.margin_xsmall
    property real preferredRightPadding: VLCStyle.margin_xsmall
    property real preferredTopPadding: VLCStyle.margin_xsmall
    property real preferredBottomPadding: VLCStyle.margin_xsmall

    leftInset: preferredLeftInset + ((view?.horizontalSpacing / 2) ?? 0.0)
    rightInset: preferredRightInset + ((view?.horizontalSpacing / 2) ?? 0.0)
    topInset: preferredTopInset + ((view?.verticalSpacing / 2) ?? 0.0)
    bottomInset: preferredBottomInset + ((view?.verticalSpacing / 2) ?? 0.0)

    leftPadding: preferredLeftPadding + ((view?.horizontalSpacing / 2) ?? 0.0)
    rightPadding: preferredRightPadding + ((view?.horizontalSpacing / 2) ?? 0.0)
    topPadding: preferredTopPadding + ((view?.verticalSpacing / 2) ?? 0.0)
    bottomPadding: preferredBottomPadding + ((view?.verticalSpacing / 2) ?? 0.0)

    width: Math.round(implicitWidth)
    height: Math.round(implicitHeight)

    property bool highlighted: (effectiveHovered || visualFocus)

    Accessible.role: Accessible.Cell
    Accessible.name: title
    Accessible.selected: root.selected
    Accessible.onPressAction: root.playClicked()

    Keys.onMenuPressed: root.contextMenuButtonClicked(picture, root.mapToGlobal(0, root.height))

    Component.onCompleted: {
        // Qt Quick AbstractButton sets a cursor for itself, unset it so that if the view has
        // busy cursor, it is visible over the delegate:
        MainCtx.unsetCursor(this)

        console.assert(root.view)
        if (root.view.delegateInstantiated)
            root.view.delegateInstantiated(root)
    }

    // States

    states: [
        State {
            name: "highlighted"
            when: highlighted

            PropertyChanges {
                target: selectedShadow
                opacity: 1.0
            }

            PropertyChanges {
                target: unselectedShadow
                opacity: 0
            }

            PropertyChanges {
                target: picture
                implicitPlayCoverShowPlay: true
            }

        }
    ]

    transitions: [
        Transition {
            from: ""
            to: "highlighted"
            // reversible: true // doesn't work

            SequentialAnimation {
                PropertyAction {
                    target: picture
                    property: "implicitPlayCoverShowPlay"
                }

                NumberAnimation {
                    properties: "opacity"
                    duration: VLCStyle.duration_long
                    easing.type: Easing.InSine
                }
            }
        },

        Transition {
            from: "highlighted"
            to: ""

            SequentialAnimation {
                PropertyAction {
                    target: picture
                    property: "implicitPlayCoverShowPlay"
                }

                NumberAnimation {
                    properties: "opacity"
                    duration: VLCStyle.duration_long
                    easing.type: Easing.OutSine
                }
            }
        }
    ]

    // Childs

    readonly property ColorContext colorContext: ColorContext {
        id: theme
        colorSet: ColorContext.Item

        focused: root.visualFocus
        hovered: root.effectiveHovered
    }

    // TODO: Qt bug 6.2: QTBUG-103604
    DoubleClickIgnoringItem {
        id: handlerParent

        anchors.fill: parent
        anchors.leftMargin: (root.view?.horizontalSpacing / 2)
        anchors.rightMargin: (root.view?.horizontalSpacing / 2)
        anchors.topMargin: (root.view?.verticalSpacing / 2)
        anchors.bottomMargin: (root.view?.verticalSpacing / 2)

        DragHandler {
            id: dragHandler

            acceptedDevices: PointerDevice.AllDevices & ~(PointerDevice.TouchScreen)

            target: null

            grabPermissions: PointerHandler.CanTakeOverFromHandlersOfDifferentType | PointerHandler.ApprovesTakeOverByAnything

            onActiveChanged: {
                if (dragItem) {
                    if (active && !selected) {
                        root.itemClicked(root._modifiersOnLastPress, true)
                    }

                    if (active)
                        dragItem.Drag.active = true
                    else
                        dragItem.Drag.drop()
                }
            }
        }

        TapHandler {
            acceptedDevices: PointerDevice.AllDevices & ~(PointerDevice.TouchScreen)

            acceptedButtons: Qt.RightButton | Qt.LeftButton

            grabPermissions: TapHandler.CanTakeOverFromHandlersOfDifferentType | TapHandler.ApprovesTakeOverByAnything

            gesturePolicy: TapHandler.ReleaseWithinBounds // TODO: Qt 6.2 bug: Use TapHandler.DragThreshold

            onSingleTapped: (eventPoint, button) => {
                initialAction()

                // FIXME: The signals are messed up in this item.
                //        Right click does not fire itemClicked?
                if (button === Qt.RightButton)
                    contextMenuButtonClicked(picture, eventPoint.globalPosition);
                else
                    root.itemClicked(point.modifiers, true);
            }

            onDoubleTapped: (eventPoint, button) => {
                if (button === Qt.LeftButton)
                    root.itemDoubleClicked(point.modifiers)
            }

            Component.onCompleted: {
                canceled.connect(initialAction)
            }

            function initialAction() {
                _modifiersOnLastPress = point.modifiers

                root.forceActiveFocus(Qt.MouseFocusReason)
            }
        }

        DelegateTouchTapHandler {
            delegate: root

            onSingleTapped: (eventPoint, button) => {
                // initial action is handled in `DelegateTouchTapHandler`
                root.itemClicked(Qt.NoModifier, false)
                root.itemDoubleClicked(Qt.NoModifier)
            }

            onDoubleTapped: (eventPoint, button) => {
                root.playClicked()
            }

            onContextMenuRequested: (index, globalPoint) => {
                root.contextMenuButtonClicked(picture ?? null, globalPoint)
            }
        }
    }

    background: AnimatedBackground {
        enabled: theme.initialized

        //don't show the backgroud unless selected
        color: root.selected ?  theme.bg.highlight : theme.bg.primary
        border.color: visualFocus ? theme.visualFocus : Qt.alpha(theme.visualFocus, 0.0)
    }

    contentItem: ColumnLayout {
        id: layout

        // Raise the content item so that the handlers of the control
        // do not handle events that are to be handled by the handlers
        // of the content item. Raising the content item should be
        // fine because content item is supposed to be the foreground
        // item.
        z: 1

        spacing: 0

        // We can not have this handler in the handler parent
        // like the rest of the handlers because of QTBUG-135886.
        // We also can not have it in the background item.
        readonly property bool hovered: hoverHandler.hovered

        HoverHandler {
            id: hoverHandler
        }

        Widgets.MediaCover {
            id: picture

            implicitPlayCoverShowPlay: false
            radius: VLCStyle.gridCover_radius
            color: theme.bg.secondary

            Layout.fillWidth: true
            Layout.preferredHeight: (root.pictureHeight / root.pictureWidth) * width
            Layout.alignment: Qt.AlignCenter

            pictureWidth: root.pictureWidth
            pictureHeight: root.pictureHeight

            onPlayIconClicked: (point) => {
                // emulate a mouse click before delivering the play signal as to select the item
                // this helps in updating the selection and restore of initial index in the parent views
                root.itemClicked(point.modifiers, true)
                root.playClicked()
            }

            Connections {
                target: root.GridView

                function onReused() {
                    picture.reinitialize()
                }
                function onPooled() {
                    picture.releaseResources()
                }
             }

            DefaultShadow {
                id: unselectedShadow

                visible: opacity > 0
            }

            DoubleShadow {
                id: selectedShadow

                visible: opacity > 0
                opacity: 0

                primaryVerticalOffset: VLCStyle.dp(6, VLCStyle.scale)
                primaryBlurRadius: VLCStyle.dp(18, VLCStyle.scale)

                secondaryVerticalOffset: VLCStyle.dp(32, VLCStyle.scale)
                secondaryBlurRadius: VLCStyle.dp(72, VLCStyle.scale)

                z: -1
            }
        }

        Widgets.TextAutoScroller {
            id: titleTextRect

            label: titleLabel
            forceScroll: root.visualFocus
            visible: root.title !== ""
            clip: scrolling

            Layout.preferredWidth: Math.min(titleLabel.implicitWidth, parent.width)
            Layout.preferredHeight: titleLabel.height
            Layout.topMargin: root.titleTopMargin
            Layout.alignment: root.textAlignHCenter ? Qt.AlignCenter : Qt.AlignLeft

            Widgets.ListLabel {
                id: titleLabel

                height: implicitHeight
                color: root.selected
                    ? theme.fg.highlight
                    : theme.fg.primary
            }
        }

        Widgets.MenuCaption {
            id: subtitleTxt

            visible: text !== ""
            text: root.subtitle
            elide: Text.ElideRight
            color: root.selected
                ? theme.fg.highlight
                : theme.fg.secondary

            Layout.preferredWidth: Math.min(parent.width, implicitWidth)
            Layout.alignment: root.textAlignHCenter ? Qt.AlignCenter : Qt.AlignLeft
            Layout.topMargin: root.subtitleTopMargin

            ToolTip.delay: VLCStyle.delayToolTipAppear
            ToolTip.text: subtitleTxt.text
            ToolTip.visible: subtitleTxtMouseHandler.hovered

            HoverHandler {
                id: subtitleTxtMouseHandler
            }
        }
    }
}
