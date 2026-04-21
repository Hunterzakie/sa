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

import QtQuick
import QtQuick.Window
import QtQuick.Controls

import VLC.MainInterface
import VLC.Style
import VLC.Util

GridView {
    id: root

    property int leftPadding: 0
    property int rightPadding: 0

    pixelAligned: (MainCtx.qtVersion() >= MainCtx.qtVersionCheck(6, 2, 5)) // QTBUG-103996
                  && (Screen.pixelDensity >= VLCStyle.highPixelDensityThreshold) // no need for sub-pixel alignment with high pixel density

    highlightFollowsCurrentItem: true

    activeFocusOnTab: true

    focus: true

    reuseItems: true

    // Content height is calculated automatically by the view.
    // FIXME: It is not calculated correctly when expansion is active,
    //        see the note in `ExpandGridView`.
    contentWidth: width - (leftMargin + rightMargin)

    // key navigation is reimplemented for item selection
    keyNavigationEnabled: false
    keyNavigationWraps: false

    flickableDirection: Flickable.AutoFlickIfNeeded

    highlightMoveDuration: 300 //ms

    boundsBehavior: Flickable.StopAtBounds

    // Provide GridSizeHelper with its required properties to
    // enable dynamic cell size:
    property GridSizeHelper gridSizeHelper

    cellWidth: (gridSizeHelper?.cellWidth ? (gridSizeHelper.cellWidth + (horizontalSpacing * 2)) : 64)
    cellHeight: (gridSizeHelper?.cellWidth ? (gridSizeHelper.cellHeight + (verticalSpacing * 2)) : 64)

    property real horizontalSpacing: gridSizeHelper?.horizontalSpacing ?? VLCStyle.column_spacing
    property real verticalSpacing: gridSizeHelper?.verticalSpacing ?? VLCStyle.column_spacing

    // NOTE: These are not applicable at the moment, they are left here because `ViewHeader` uses it:
    property real contentLeftMargin: horizontalSpacing + leftPadding
    property real contentRightMargin: horizontalSpacing + rightPadding

    readonly property real maxPictureWidth: gridSizeHelper?.maxPictureWidth ?? 0.0
    readonly property real maxPictureHeight: gridSizeHelper?.maxPictureHeight ?? 0.0

    property real titleTopMargin: gridSizeHelper?.titleTopMargin ?? VLCStyle.gridItemTitle_topMargin
    property real titleHeight: gridSizeHelper?.titleHeight ?? VLCStyle.gridItemTitle_height

    property real subtitleTopMargin: gridSizeHelper?.subtitleTopMargin ?? VLCStyle.gridItemSubtitle_topMargin
    property real subtitleHeight: gridSizeHelper?.subtitleHeight ?? VLCStyle.gridItemSubtitle_height

    Accessible.role: Accessible.Table

    property ListSelectionModel selectionModel: ListSelectionModel {
        model: root.model
    }

    readonly property int columnCount: Math.floor(contentWidth / cellWidth)

    property bool _releaseActionButtonPressed

    property int _currentFocusReason: Qt.OtherFocusReason

    // Useful for batch rendering when the delegate draws things outside the cell, which
    // is the case with shadow effect. Note that this change affects how the view looks,
    // if the overlapping parts of delegate are not using commutative blending (which
    // is the case by default with source-over blending in scene graph), but it is not
    // really obvious with the shadows. Note that item delegate must emit view signal
    // `onDelegateInstantiated` with a parameter of itself when instantiated for this
    // to work.
    property bool delegateZTiling: true

    signal delegateInstantiated(Item instance)

    onDelegateInstantiated: (instance /*: Item*/) => {
        console.assert(instance)
        delegateZTilingBindingComponent.incubateObject(instance,
                                                       {'targetItem': instance},
                                                       1 /* QQmlIncubator::AsynchronousIfNested */)
    }

    //signals emitted when selected items is updated from keyboard
    signal selectAll()
    signal actionAtIndex(int index)

    signal showContextMenu(point globalPos)

    property Component defaultScrollBar: Component {
        ScrollBarExt { }
    }

    ScrollBar.vertical: {
        if (root.defaultScrollBar)
            return root.defaultScrollBar.createObject() // rely on JS/QML engine's garbage collection
        return null
    }

    Behavior on contentY {
        id: contentYBehavior

        enabled: false

        // NOTE: Usage of `SmoothedAnimation` is intentional here.
        SmoothedAnimation {
            duration: VLCStyle.duration_veryLong
            easing.type: Easing.InOutSine
        }
    }

    Component.onCompleted: {
        if (!usingTouch) {
            // Flickable filters child mouse events for flicking (even when
            // the delegate is grabbed). However, this is not a useful
            // feature for non-touch cases, so disable it here and enable
            // it if touch is detected through the hover handler:
            MainCtx.setFiltersChildMouseEvents(root, false)
        }
    }

    readonly property bool usingTouch: MainCtx.usingTouch

    onUsingTouchChanged: {
        if (usingTouch)
            MainCtx.setFiltersChildMouseEvents(root, true)
        // We do not disable filtering child mouse events
        // because Qt currently has a bug that the flickable
        // jumps when it is enabled again later on.
    }

    function leftClickOnItem(modifier, index, select) {
        if (select)
            selectionModel.updateSelection(modifier, currentIndex, index)

        currentIndex = index

        // NOTE: We make sure to clear the keyboard focus.
        if (currentItem)
            currentItem.forceActiveFocus(Qt.MouseFocusReason);
    }

    function rightClickOnItem(index) {
        if (!selectionModel.isSelected(index)) {
            leftClickOnItem(Qt.NoModifier, index, true)
        }
    }

    function setCurrentItemFocus(reason) {

        // NOTE: Saving the focus reason for later.
        _currentFocusReason = reason;

        if (!model || model.count === 0) {
            // NOTE: By default we want the focus on the flickable.
            forceActiveFocus(reason);
            return;
        }

        if (currentIndex === -1)
            currentIndex = 0

        if (currentIndex < count)
            Helpers.enforceFocus(currentItem, reason);
        else
            forceActiveFocus(reason);

        // NOTE: We make sure the current item is fully visible.
        positionViewAtIndex(currentIndex, ItemView.Contain);
    }

    Keys.onPressed: (event) => {
        let newIndex = -1
        const nbItemPerRow = root.columnCount
        const _count = root.count
        if (KeyHelper.matchRight(event)) {
            if ((currentIndex + 1) % nbItemPerRow !== 0) {//are we not at the end of line
                newIndex = Math.min(_count - 1, currentIndex + 1)
            }
        } else if (KeyHelper.matchLeft(event)) {
            if (currentIndex % nbItemPerRow !== 0) {//are we not at the beginning of line
                newIndex = Math.max(0, currentIndex - 1)
            }
        } else if (KeyHelper.matchDown(event)) {
            const lastIndex = _count - 1
            // we are not on the last line
            if (Math.floor(currentIndex / nbItemPerRow)
                !==
                Math.floor(lastIndex / nbItemPerRow)) {
                newIndex = Math.min(lastIndex, currentIndex + nbItemPerRow)
            }
        } else if (KeyHelper.matchPageDown(event)) {
            newIndex = Math.min(_count - 1, currentIndex + nbItemPerRow * 5)
        } else if (KeyHelper.matchUp(event)) {
            if (Math.floor(currentIndex / nbItemPerRow) !== 0) { //we are not on the first line
                newIndex = Math.max(0, currentIndex - nbItemPerRow)
            }
        } else if (KeyHelper.matchPageUp(event)) {
            newIndex = Math.max(0, currentIndex - nbItemPerRow * 5)
        } else if (event.matches(StandardKey.SelectAll)) {
            event.accepted = true
            selectionModel.selectAll()
        }

        if (KeyHelper.matchOk(event)) {
            // Keep activation handling on release to avoid double interpretation.
            event.accepted = true
            _releaseActionButtonPressed = true
        } else {
            _releaseActionButtonPressed = false
        }

        if (newIndex !== -1 && newIndex !== currentIndex) {
            event.accepted = true;

            const oldIndex = currentIndex;
            currentIndex = newIndex;
            if (selectionModel)
                selectionModel.updateSelection(event.modifiers, oldIndex, newIndex)

            if (root.highlightFollowsCurrentItem) {
                if (root.headerItem && (root.headerPositioning !== ListView.InlineHeader)) {
                    if (root.currentItem.y < (root.headerItem.y + root.headerItem.height))
                        positionViewAtIndex(currentIndex, ItemView.Contain);
                }

                if (root.footerItem && (root.footerPositioning !== ListView.InlineFooter)) {
                    if (root.currentItem.y > root.footerItem.y)
                        positionViewAtIndex(currentIndex, ItemView.Contain);
                }
            }

            if (oldIndex < currentIndex)
                Helpers.enforceFocus(currentItem, Qt.TabFocusReason);
            else
                Helpers.enforceFocus(currentItem, Qt.BacktabFocusReason);
        }

        if (!event.accepted) {
            Navigation.defaultKeyAction(event)
        }
    }

    Keys.onReleased: (event) => {
        if (!_releaseActionButtonPressed)
            return

        if ( KeyHelper.matchOk(event) ) {
            event.accepted = true
            actionAtIndex(currentIndex)
        }
        _releaseActionButtonPressed = false
    }

    Binding on maximumFlickVelocity {
        id: maximumFlickVelocitySuppressor

        when: false
        value: -1
    }

    function positionViewAtBeginningAnimated() {
        if (!root.flickTo) { // flickTo was added in Qt 6.11
            positionViewAtBeginning()
            return
        }

        // flickTo() velocity is capped by maximumFlickVelocity, meaning it won't reach
        // the top in a big list. Suppress the cap only for the flickTo() call
        maximumFlickVelocitySuppressor.when = true
        root.flickTo(Qt.point(root.originX, root.originY))
        maximumFlickVelocitySuppressor.when = false
    }

    readonly property ColorContext colorContext: ColorContext {
        id: theme
        colorSet: ColorContext.View
    }

    TapHandler {
        acceptedDevices: PointerDevice.AllDevices & ~(PointerDevice.TouchScreen)

        acceptedButtons: Qt.LeftButton | Qt.RightButton

        grabPermissions: PointerHandler.TakeOverForbidden

        gesturePolicy: TapHandler.ReleaseWithinBounds

        onTapped: (eventPoint, button) => {
            initialAction()

            if (button === Qt.RightButton) {
                root.showContextMenu(eventPoint.globalPosition)
            }
        }

        Component.onCompleted: {
            canceled.connect(initialAction)
        }

        function initialAction() {
            if (root.currentItem)
                root.currentItem.focus = false // Grab the focus from delegate
            root.forceActiveFocus(Qt.MouseFocusReason) // Re-focus the list

            if (!(point.modifiers & (Qt.ShiftModifier | Qt.ControlModifier))) {
                if (root.selectionModel)
                    root.selectionModel.clearSelection()
            }

            root.retract()
        }
    }

    TapHandler {
        acceptedDevices: PointerDevice.TouchScreen

        grabPermissions: PointerHandler.TakeOverForbidden

        gesturePolicy: TapHandler.ReleaseWithinBounds

        property bool pendingContextMenu: false

        onSingleTapped: (eventPoint, button) => {
            initialAction()
        }

        onLongPressed: {
            initialAction()

            pendingContextMenu = true
        }

        onPressedChanged: {
            if (!pressed) {
                if (pendingContextMenu) {
                    root.showContextMenu(parent.mapToGlobal(point.position.x, point.position.y))
                    pendingContextMenu = false
                }
            }
        }

        onCanceled: {
            pendingContextMenu = false
        }

        function initialAction() {
            if (root.currentItem)
                root.currentItem.focus = false // Grab the focus from delegate
            root.forceActiveFocus(Qt.MouseFocusReason) // Re-focus the list

            root.currentIndex = -1
        }
    }
    
    property Component implicitFlickableScrollHandler: DefaultFlickableScrollHandler { }

    // NOTE: This property can be set to null to prevent using a scroll handler:
    property FlickableScrollHandler scrollHandler: {
        // Make sure the JS engine destroys the scroll handler right after (potential) change:
        Qt.callLater(gc) // `QJSEngine::GarbageCollectionExtension` is installed by default

        if (interactive) {
            // JS ownership:
            return implicitFlickableScrollHandler.createObject(null, { target: root })
        } else {
            return null
        }
    }

    Component {
        id: delegateZTilingBindingComponent

        Binding {
            required property Item targetItem

            target: targetItem

            when: root.delegateZTiling

            // We can do this, as the view we can manage the `z` of delegate.
            property: "z"

            value: {
                if (!targetItem)
                    return

                const delegateIndex = targetItem.index
                console.assert(Number.isInteger(delegateIndex))

                const row = Math.floor(delegateIndex / root.columnCount)
                const col = delegateIndex % root.columnCount

                // Divide by 10 to create a z group (0 to 1) for this purpose, so
                // that non-delegate items can remain stacked properly with regard
                // to their z. Note that `z` does not need to be integer for items:
                return (row % 2 + 2 * (col % 2)) / 10.0
            }
        }
    }
}
