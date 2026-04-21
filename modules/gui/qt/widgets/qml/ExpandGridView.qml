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

import VLC.MainInterface
import VLC.Style
import VLC.Util

GridViewExt {
    id: root

    property bool isAnimating: expandAnimation.running

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

    onDelegateInstantiated: (instance /*: Item*/) => {
        console.assert(instance)
        delegateExpansionCompensateTranslateComponent.incubateObject(instance,
                                                                     {'targetItem': instance},
                                                                     1 /* QQmlIncubator::AsynchronousIfNested */)
    }

    Binding {
        target: root.expandee?.GridView ?? null
        property: "delayRemove"
        when: expandLoader.active
        value: true
    }

    Binding on highlightFollowsCurrentItem {
        when: expandLoader.active
        value: false // force disable
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
}
