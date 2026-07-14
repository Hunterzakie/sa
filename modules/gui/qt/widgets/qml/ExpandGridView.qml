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

    // NOTE: These are not applicable at the moment, they are left here because `ViewHeader` uses it:
    property real contentLeftMargin: leftPadding
    property real contentRightMargin: rightPadding

    pixelAligned: (MainCtx.qtVersion() >= MainCtx.qtVersionCheck(6, 2, 5)) // QTBUG-103996
                  && (Screen.pixelDensity >= VLCStyle.highPixelDensityThreshold) // no need for sub-pixel alignment with high pixel density

    highlightFollowsCurrentItem: true

    Binding on highlightFollowsCurrentItem {
        when: expandLoader.active
        value: false // force disable
    }

    activeFocusOnTab: true

    focus: true

    reuseItems: true

    // Content height is calculated automatically by the view.
    // FIXME: It is not calculated correctly when expansion is active,
    //        see the note in <expansion>.
    contentWidth: width - (leftMargin + rightMargin)

    // key navigation is reimplemented for item selection
    keyNavigationEnabled: false
    keyNavigationWraps: false

    flickableDirection: Flickable.AutoFlickIfNeeded

    highlightMoveDuration: 300 //ms

    boundsBehavior: Flickable.StopAtBounds

    Accessible.role: Accessible.Table

    property ListSelectionModel selectionModel: ListSelectionModel {
        model: root.model
    }

    property bool isAnimating: expandAnimation.running

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
        delegateExpansionCompensateTranslateComponent.incubateObject(instance,
                                                                     {'targetItem': instance},
                                                                     1 /* QQmlIncubator::AsynchronousIfNested */)
    }

    // WARNING: Item delegate must emit `onDelegateInstantiated`
    //          signal of the view with a parameter of itself
    //          when instantiated for expansion to work.

    // Expansion properties:
    property alias expandDelegate: expandLoader.sourceComponent
    property alias expandItem: expandLoader.item
    property alias expandLoadAsynchronously: expandLoader.asynchronous
    expandLoadAsynchronously: true
    property alias animateExpansion: expandLoaderHeightBehavior.enabled
    animateExpansion: true

    readonly property int expandIndex: expandLoader.index
    readonly property Item expandee: itemAtIndex(expandIndex)

    function retract() : bool {
        return expandLoader.retract()
    }

    function expand(index : int) : bool {
        return switchExpandItem(index)
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
    
    /// <expansion>

    Binding {
        target: root.expandee?.GridView ?? null
        property: "delayRemove"
        when: expandLoader.active
        value: true
    }

    Connections {
        target: root.expandee?.GridView ?? null

        function onRemove() {
            expandLoader.retract(false)
        }

        function onPooled() {
            expandLoader.retract(false)
        }
    }

    onModelChanged: {
        expandLoader.retract(false)
    }

    Connections {
        target: root.model

        function onRowsInserted() { expandLoader.retract(false) }
        function onRowsRemoved() { expandLoader.retract(false) }
        function onModelReset() { expandLoader.retract(false) }
        function onLayoutChanged() { expandLoader.retract(false) }
    }

    Loader {
        id: expandLoader

        parent: root.contentItem

        anchors.left: parent.left
        anchors.right: parent.right

        active: false
        height: 0

        clip: expandAnimation.running && (height < implicitHeight)

        Navigation.parentItem: root

        Navigation.upAction: function() {
            if (root.expandee) {
                root.expandee.forceActiveFocus(Qt.BacktabFocusReason)
                root.currentIndex = root.expandIndex
                return
            }

            root.Navigation.defaultNavigationUp()
        }

        Navigation.downAction: function() {
            for (let i = root.expandIndex + 1; i < root.count; ++i) {
                const item = root.itemAtIndex(i)
                if (item && (item.y > root.expandee.y)) {
                    item.forceActiveFocus(Qt.TabFocusReason)
                    root.currentIndex = item.index
                    return
                }
            }

            root.Navigation.defaultNavigationDown()
        }

        Behavior on height {
            id: expandLoaderHeightBehavior

            SmoothedAnimation {
                id: expandAnimation

                duration: VLCStyle.duration_long
                easing.type: Easing.InOutSine
            }
        }

        function retract(animate /* : bool */ = true) : bool {
            if (!active)
                return false

            const behaviorEnabled = expandLoaderHeightBehavior.enabled
            if (!animate) {
                expandLoaderHeightBehavior.enabled = false
            }

            height = 0.0
            active = Qt.binding(() => (expandLoader.height > 0.0))

            if (!animate) {
                expandLoaderHeightBehavior.enabled = behaviorEnabled
            }

            return true
        }

        function expand(animate /* : bool */ = true) : bool {
            if (active)
                return false

            if (!sourceComponent)
                return false

            const behaviorEnabled = expandLoaderHeightBehavior.enabled
            if (!animate) {
                expandLoaderHeightBehavior.enabled = false
            }

            console.assert(index >= 0)
            active = true
            height = Qt.binding(() => (expandLoader.implicitHeight))

            if (!animate) {
                expandLoaderHeightBehavior.enabled = expandLoaderHeightBehavior.enabled
            }

            return true
        }

        y: root.expandee ? (root.expandee.y + root.cellHeight) : 0.0

        property int index: -1

        onActiveChanged: {
            _oldHeight = 0

            if (!active) {
                 expandLoader.index = -1

                if (root._pendingExpandIndex >= 0) {
                    root.switchExpandItem(root._pendingExpandIndex)
                    root._pendingExpandIndex = -1
                }
            }
        }

        property real _oldHeight

        onHeightChanged: {
            if ((height + y - root.contentY) > (root.height - root._baseDisplayMarginEnd)) {
                root.contentY += (height - _oldHeight)
            }

            _oldHeight = height
        }

        onLoaded: {
            // Can not use `!== undefined` because the value may be undefined even though it exists
            if (item.hasOwnProperty("model"))
                item.model = Qt.binding(() => root.model.getDataAt(root.expandIndex))
            if (item.index !== undefined)
                item.index = Qt.binding(() => expandLoader.index)
            if (item.view !== undefined)
                item.view = root

            if (item.leftPadding !== undefined)
                item.leftPadding = Qt.binding(function() { return VLCStyle.margin_large + VLCStyle.applicationHorizontalMargin })
            if (item.rightPadding !== undefined)
                item.rightPadding = Qt.binding(function() { return VLCStyle.margin_large + VLCStyle.applicationHorizontalMargin })

            if (item.retract !== undefined)
                item.retract.connect(root, root.retract)

            item.Navigation.parentItem = expandLoader

            item.focus = true // Loader itself is a focus scope, so we need this
        }
    }

    property real _baseContentHeight
    property real _baseBottomMargin
    property real _baseDisplayMarginEnd

    property int _pendingExpandIndex: -1

    function switchExpandItem(index : int) {
        if (!expandLoader.active) {
            expandLoader.index = index

            _baseContentHeight = root.contentHeight
            _baseBottomMargin = root.bottomMargin
            _baseDisplayMarginEnd = root.displayMarginEnd

            expandLoader.expand()
        } else {
            // If the new expandee is in the same row, we don't retract first but switch
            // immediately:
            if (expandLoader.index === index) {
                expandLoader.retract()
            } else if (root.expandee) {
                if (Math.abs(root.expandee.y - root.itemAtIndex(index).y) >= Number.EPSILON) {
                    expandLoader.retract()
                    root._pendingExpandIndex = index
                } else {
                    expandLoader.index = index
                }
            }
        }
    }

    // Qt overwrites the binding, so this is not effective at the moment.
    // As a workaround, we adjust the bottom margin instead.

    // Binding on contentHeight {
    //     when: expandLoader.active
    //     value: root._baseContentHeight + expandLoader.height
    // }

    // FIXME: Get rid of this when we can adjust the content height ourselves:
    Binding on bottomMargin {
        when: expandLoader.active
        value: root._baseBottomMargin + expandLoader.height
    }

    Binding on displayMarginEnd {
        when: expandLoader.active
        value: root._baseDisplayMarginEnd + expandLoader.height
    }

    Binding on displayMarginBeginning {
        when: expandLoader.active
        value: root._baseDisplayMarginEnd + expandLoader.height
    }

    Component {
        id: delegateExpansionCompensateTranslateComponent

        Translate {
            objectName: "expansionCompensateTranslate"

            required property Item targetItem

            y: {
                if (expandLoader.active && targetItem) {
                    if (targetItem.y > root.expandee.y) {
                        return expandLoader.height
                    }
                }

                return 0.0
            }

            Component.onCompleted: {
                console.assert(targetItem)
                targetItem.transform.push(this)
            }
        }
    }

    /// </expansion>
}
