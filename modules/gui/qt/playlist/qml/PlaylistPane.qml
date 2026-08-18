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
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import QtQml.Models


import VLC.MainInterface
import VLC.Widgets as Widgets
import VLC.Util
import VLC.Playlist
import VLC.Style

T.Pane {
    id: root

    property var model: PlaylistListModel {
        playlist: MainPlaylistController.playlist
    }
    readonly property ListSelectionModel selectionModel: listView?.selectionModel ?? null

    property bool useAcrylic: true

    readonly property real minimumWidth: contentItem.Layout.minimumWidth +
                                         leftPadding +
                                         rightPadding +
                                         2 * (VLCStyle.margin_xsmall)

    readonly property ListView listView: contentItem.listView

    property alias contextMenu: contextMenu

    property alias dragItem: dragItem

    property alias isDropAcceptableFunc: listView.isDropAcceptableFunc
    property alias acceptDropFunc: listView.acceptDropFunc

    property alias view: listView

    // Detailed mode, akin to VLC 3.0's "Detailed List":
    property bool detailed: MainCtx.playlistDetailed

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    verticalPadding: VLCStyle.margin_normal

    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Play Queue")
    AccessibleCompat.id: "playqueuePane"

    readonly property ColorContext colorContext: ColorContext {
        id: theme
        colorSet: ColorContext.View

        focused: root.activeFocus
        hovered: root.hovered
        enabled: root.enabled
    }

    Widgets.DragItem {
        id: dragItem

        view: root.listView

        onRequestData: (indexes, resolve, reject) => {
            resolve(indexes.map((index) => {
                const item = root.model.itemAt(index)
                return {
                    "title": item.title,
                    "cover": (!!item.artwork && item.artwork.toString() !== "") ? item.artwork : VLCStyle.noArtAlbumCover,
                    "url": item.url
                }
            }))
        }

        onRequestInputItems: (indexes, data, resolve, reject) => {
            resolve(root.model.getItemsForIndexes((indexes?.length > 0) ? indexes : root.selectionModel.selectedIndexesFlat))
        }
    }

    PlaylistContextMenu {
        id: contextMenu
        model: root.model
        proxyModel: (root.contentItem?.listView?.model instanceof AbstractProxyModel) ? root.contentItem.listView.model
                                                                                        : null
        selectionModel: root.selectionModel
        controler: MainPlaylistController
        ctx: MainCtx

        onJumpToCurrentPlaying: listView.positionViewAtIndex( MainPlaylistController.currentIndex, ItemView.Center)
    }

    background: Widgets.AcrylicBackground {
        enabled: root.useAcrylic
        tintColor: theme.bg.primary
    }

    contentItem: ColumnLayout {
        spacing: VLCStyle.margin_xxsmall

        Layout.minimumWidth: Math.min(Math.max(noContentInfoColumn.implicitWidth,
                                               titleRowLayout.implicitWidth ),
                                      /* Minimum width should not be too large, we rely on elision otherwise: */
                                      Window.width / 3) + (headerColumn.Layout.leftMargin + headerColumn.Layout.rightMargin)

        readonly property ListView listView: listView

        Column {
            id: headerColumn

            Layout.fillHeight: false
            Layout.fillWidth: true
            Layout.leftMargin: VLCStyle.margin_normal
            Layout.rightMargin: VLCStyle.margin_normal

            spacing: VLCStyle.margin_xxxsmall

            RowLayout {
                id: titleRowLayout

                anchors.left: parent.left
                anchors.right: parent.right

                spacing: VLCStyle.margin_normal

                Widgets.SubtitleLabel {
                    id: titleLabel

                    Layout.fillWidth: true

                    text: qsTr("Play Queue")
                    color: theme.fg.primary
                    font.weight: Font.Bold
                    font.pixelSize: VLCStyle.dp(24, VLCStyle.scale)
                }

                Widgets.IconToolButton {
                    text: root.detailed ? VLCIcons.list
                                        : VLCIcons.ic_fluent_apps_list_detail_24_regular

                    description: qsTr("Detailed list")

                    font.pixelSize: VLCStyle.icon_banner

                    onClicked: {
                        MainCtx.playlistDetailed = !MainCtx.playlistDetailed
                    }
                }

                Widgets.SortControl {
                    id: sort

                    font.pixelSize: VLCStyle.icon_banner

                    description : qsTr("Sort")

                    AccessibleCompat.id: "playqueueSort"

                    enabled: MainPlaylistController.count > 1

                    checked: MainPlaylistController.sortKey !== PlaylistController.SORT_KEY_NONE

                    popupAbove: false

                    model: MainPlaylistController.sortKeyTitleList

                    onSortSelected: key => {
                        MainPlaylistController.sortKey = key
                    }

                    onSortOrderSelected: type => {
                        if (type === Qt.AscendingOrder)
                            MainPlaylistController.sortOrder = PlaylistController.SORT_ORDER_ASC
                        else if (type === Qt.DescendingOrder)
                            MainPlaylistController.sortOrder = PlaylistController.SORT_ORDER_DESC

                        MainPlaylistController.sort()
                    }

                    sortOrder: {
                        if (MainPlaylistController.sortOrder === PlaylistController.SORT_ORDER_ASC) {
                            Qt.AscendingOrder
                        }
                        else if (MainPlaylistController.sortOrder === PlaylistController.SORT_ORDER_DESC) {
                            Qt.DescendingOrder
                        }
                    }

                    sortKey: MainPlaylistController.sortKey

                    Navigation.parentItem: root
                    Navigation.rightItem: searchBox
                    Navigation.downItem: listView
                }

                Widgets.SearchBox {
                    id: searchBox

                    enabled: MainPlaylistController.count > 1

                    toggleButton.font.pixelSize: VLCStyle.icon_banner

                    popBelow: true

                    displayRegexToggleButton: true
                    displayCaseSensitiveToggleButton: true

                    popup.width: Math.max(popup.implicitWidth, root.contentItem?.width * 0.6)
                    popup.topPadding: VLCStyle.margin_xxsmall

                    Navigation.parentItem: root
                    Navigation.leftItem: sort
                    Navigation.downItem: listView

                    Connections {
                        target: contextMenu

                        function onDiscardedFilteredOutItems() {
                            searchBox.retract()
                        }
                    }
                }
            }

            Widgets.CaptionLabel {
                color: theme.fg.secondary
                visible: listView.count !== 0
                text: {
                    if (listView.count <= 0)
                        return "" // Not visible otherwise

                    let duration
                    const model = listView.model
                    if (model instanceof PlaylistListModel) {
                        duration = model.duration
                    } else {
                        // Proxy model, we need to use `QAbstractItemModel` api:

                        duration = []
                        for (let i = 0; i < listView.count; ++i) {
                            const itemDuration = model.data(model.index(i, 0), PlaylistListModel.DurationRole)
                            console.assert(itemDuration !== undefined && itemDuration !== null) // typeof `vlcDuration`
                            duration.push(itemDuration)
                        }
                        duration = duration[0].sum(duration)
                    }

                    return qsTr("%1 elements, %2").arg(listView.count).arg(duration.formatLong())
                }
            }
        }

        Item {
            // Spacer

            implicitHeight: VLCStyle.margin_xsmall
        }

        Loader {
            id: headerLoader

            z: 1

            active: listView.count !== 0
            visible: active

            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.leftMargin: VLCStyle.margin_normal
            Layout.rightMargin: Math.max(listView.ScrollBar.vertical.width, VLCStyle.margin_normal)

            sourceComponent: root.detailed ? detailedHeaderComponent : normalHeaderComponent

            Component {
                id: normalHeaderComponent

                RowLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right

                    spacing: VLCStyle.margin_large

                    Widgets.IconLabel {
                        // play queue cover column
                        Layout.preferredWidth: VLCStyle.icon_playlistArt

                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: VLCIcons.album_cover
                        font.pixelSize: VLCStyle.icon_playlistHeader

                        color: theme.fg.secondary

                        Accessible.role: Accessible.ColumnHeader
                        Accessible.name: qsTr("Cover")
                        Accessible.ignored: false
                    }

                    //Use Text here as we're redefining its Accessible.role
                    Text {
                        Layout.fillWidth: true

                        elide: Text.ElideRight
                        font.pixelSize: VLCStyle.fontSize_normal
                        textFormat: Text.PlainText

                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("Title")
                        color: theme.fg.secondary

                        Accessible.role: Accessible.ColumnHeader
                        Accessible.name: text
                    }

                    Widgets.IconLabel {
                        Layout.preferredWidth: durationMetric.width

                        text: VLCIcons.time
                        color: theme.fg.secondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: VLCStyle.icon_playlistHeader

                        Accessible.role: Accessible.ColumnHeader
                        Accessible.name: qsTr("Duration")
                        Accessible.ignored: false

                        TextMetrics {
                            id: durationMetric

                            font.weight: Font.DemiBold
                            font.pixelSize: VLCStyle.fontSize_normal
                            text: "00:00"
                        }
                    }
                }
            }

            Component {
                id: detailedHeaderComponent

                T.SplitView {
                    id: splitView

                    anchors.left: parent.left
                    anchors.right: parent.right

                    // FIXME: We should not need this normally, but `SplitView` does not calculate
                    //        implicit height at the moment (horizontal mode):
                    height: VLCStyle.fontSize_normal + (2 * VLCStyle.margin_xxsmall)

                    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                                            implicitContentWidth + leftPadding + rightPadding)
                    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                                             implicitContentHeight + topPadding + bottomPadding)

                    property real contentTextHorizontalPadding: VLCStyle.margin_xxsmall
                    property real handleWidth: MainCtx.usingTouch ? VLCStyle.dp(6, VLCStyle.scale)
                                                                  : VLCStyle.dp(3, VLCStyle.scale)

                    readonly property string columnsSettingKey: "Playlist/DetailedHeader/Columns"
                    readonly property string stateSettingKey: "Playlist/DetailedHeader/State"

                    property alias titleItem: titleItem

                    // You can access the items inside split view through this (cached) property,
                    // which is useful to get the width to match in the delegate:
                    readonly property var headerItemMap: {
                        let ret = {}

                        for (let i in contentChildren) { // for .. of is broken in Qt 6.2
                            const item = contentChildren[i]
                            if (item) {
                                ret[item.objectName] = item
                            }
                        }

                        return ret
                    }

                    SelectableQmlPropertyMap {
                        id: headerMap

                        Component.onCompleted: {
                            // The order should match with the detailed delegate for proper x alignment (`PlaylistDetailedDelegate`):
                            headerMap.duration = { role: PlaylistListModel.DurationRole,
                                                   actionText: qsTr("Duration"),
                                                   sortKey: PlaylistController.SORT_KEY_DURATION }
                            headerMap.artist = { role: PlaylistListModel.ArtistRole,
                                                 actionText: qsTr("Artist"),
                                                 sortKey: PlaylistController.SORT_KEY_ARTIST }
                            headerMap.genre = { role: PlaylistListModel.GenreRole,
                                                actionText: qsTr("Genre"),
                                                sortKey: PlaylistController.SORT_KEY_GENRE }
                            headerMap.album = { role: PlaylistListModel.AlbumRole,
                                                actionText: qsTr("Album"),
                                                sortKey: PlaylistController.SORT_KEY_ALBUM }
                            headerMap.trackNumber = { role: PlaylistListModel.TrackNumberRole,
                                                      actionText: qsTr("Track number"),
                                                      sortKey: PlaylistController.SORT_KEY_TRACK_NUMBER }
                            headerMap.description = { role: PlaylistListModel.DescriptionRole,
                                                      actionText: qsTr("Description"),
                                                      /* sortKey: PlaylistController.SORT_KEY_DESCRIPTION */ }
                            headerMap.url = { role: PlaylistListModel.UrlRole,
                                              actionText: qsTr("URL"),
                                              sortKey: PlaylistController.SORT_KEY_URL }
                            headerMap.trackId = { role: PlaylistListModel.TrackIdRole,
                                                  actionText: qsTr("Track ID"),
                                                  /* sortKey: PlaylistController.SORT_KEY_TRACK_ID */ }
                            headerMap.rating = { role: PlaylistListModel.RatingRole,
                                                 actionText: qsTr("Rating"),
                                                 sortKey: PlaylistController.SORT_KEY_RATING }
                            headerMap.discNumber = { role: PlaylistListModel.DiscNumberRole,
                                                     actionText: qsTr("Disc number"),
                                                     sortKey: PlaylistController.SORT_KEY_DISC_NUMBER }
                            headerMap.date = { role: PlaylistListModel.DateRole,
                                               actionText: qsTr("Date"),
                                               sortKey: PlaylistController.SORT_KEY_DATE }

                            headerMap.freeze() // Only for optimization

                            const columns = MainCtx.settingValue(splitView.columnsSettingKey, undefined)
                            if (columns) {
                                headerMap.select(columns, SelectableQmlPropertyMap.Select)
                            } else {
                                // VLC 3.0 has duration and album by default:
                                headerMap.select(["duration", "album"], SelectableQmlPropertyMap.Select)
                            }

                            const state = MainCtx.settingValue(splitView.stateSettingKey, undefined)
                            if (state) {
                                splitView.restoreState(state)
                            }
                        }

                        Component.onDestruction: {
                            const state = splitView.saveState()
                            if (state) {
                                MainCtx.setSettingValue(splitView.stateSettingKey, state)
                            }

                            MainCtx.setSettingValue(splitView.columnsSettingKey, headerMap.selectedItems)
                        }
                    }

                    background: Item {

                        // TODO: Qt bug 6.2: QTBUG-103604
                        DoubleClickIgnoringItem {
                            id: tapArea

                            parent: splitView
                            anchors.fill: parent

                            z: 1

                            TapHandler {
                                target: splitView

                                acceptedDevices: PointerDevice.AllDevices & ~(PointerDevice.TouchScreen)

                                acceptedButtons: Qt.RightButton

                                gesturePolicy: TapHandler.ReleaseWithinBounds // TODO: Qt 6.2 bug: Use TapHandler.DragThreshold

                                grabPermissions: TapHandler.CanTakeOverFromHandlersOfDifferentType | TapHandler.ApprovesTakeOverByAnything

                                onSingleTapped: (eventPoint, button) => {
                                    initialAction()

                                    if (button === Qt.RightButton)
                                        headerMap.popupCheckableMenu(eventPoint.globalPosition, MainCtx)
                                }

                                Component.onCompleted: {
                                    canceled.connect(initialAction)
                                }

                                function initialAction() {
                                    splitView.forceActiveFocus(Qt.MouseFocusReason)
                                }
                            }

                            TapHandler {
                                id: touchTapHandler

                                target: splitView

                                acceptedDevices: PointerDevice.TouchScreen

                                grabPermissions: TapHandler.CanTakeOverFromHandlersOfDifferentType | TapHandler.ApprovesTakeOverByAnything

                                property bool pendingContextMenu: false

                                onSingleTapped: (eventPoint, button) => {
                                    initialAction()
                                }

                                onLongPressed: {
                                    initialAction()
                                    pendingContextMenu = true
                                }

                                function invokeContextMenu(point : point) : bool {
                                    if (touchTapHandler?.pendingContextMenu) {
                                        headerMap.popupCheckableMenu(point, MainCtx)
                                    }
                                }

                                onPressedChanged: {
                                    if (!pressed) {
                                        // We need to do it asynchronously, because if tapping is canceled we need to acknowledge it:
                                        Qt.callLater(touchTapHandler.invokeContextMenu, parent.mapToGlobal(point.position.x,
                                                                                                           point.position.y))
                                    }
                                }

                                onCanceled: {
                                    pendingContextMenu = false
                                }

                                function initialAction() {
                                    splitView.forceActiveFocus(Qt.MouseFocusReason)
                                }
                            }
                        }
                    }

                    handle: Rectangle {
                        implicitWidth: splitView.handleWidth
                        color: "transparent" // theme.border
                    }

                    component SplitViewDelegate : RowLayout {
                        id: delegate

                        T.SplitView.minimumWidth: VLCStyle.margin_xlarge

                        property alias text: textItem.text

                        property int sortKey: -1

                        Text {
                            id: textItem

                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            leftPadding: splitView.contentTextHorizontalPadding
                            rightPadding: !sortIndicator.visible ? splitView.contentTextHorizontalPadding : 0.0

                            elide: Text.ElideRight
                            font.pixelSize: VLCStyle.fontSize_normal
                            textFormat: Text.PlainText

                            verticalAlignment: Text.AlignVCenter

                            color: theme.fg.secondary

                            Accessible.role: Accessible.ColumnHeader
                            Accessible.name: text

                            T.ToolTip.visible: (elide !== Text.ElideNone) && (width < implicitWidth) && hoverHandler.hovered
                            T.ToolTip.delay: VLCStyle.delayToolTipAppear
                            T.ToolTip.text: text

                            property alias hovered: hoverHandler.hovered

                            HoverHandler {
                                id: hoverHandler
                            }
                        }

                        // Akin to how it is shown in media library table views:
                        Text {
                            id: sortIndicator

                            Layout.fillHeight: true

                            text: (MainPlaylistController.sortOrder === Qt.AscendingOrder) ? "▼" : "▲"
                            visible: (MainPlaylistController.sortKey === ((delegate.sortKey >= 0) ? delegate.sortKey : headerMap[delegate.objectName]?.sortKey))
                            font.pixelSize: VLCStyle.fontSize_normal
                            color: theme.accent

                            rightPadding: splitView.contentTextHorizontalPadding
                        }

                        TapHandler {
                            onTapped: (eventPoint, button) => {
                                const sortKey = (delegate.sortKey >= 0) ? delegate.sortKey : headerMap[delegate.objectName]?.sortKey
                                if (sortKey !== undefined) {
                                    if ((MainPlaylistController.sortKey !== sortKey))
                                        MainPlaylistController.sortKey = sortKey
                                    else if (MainPlaylistController.sortOrder === PlaylistController.SORT_ORDER_ASC)
                                        MainPlaylistController.sortOrder = PlaylistController.SORT_ORDER_DESC
                                    else if (MainPlaylistController.sortOrder === PlaylistController.SORT_ORDER_DESC)
                                        MainPlaylistController.sortOrder = PlaylistController.SORT_ORDER_ASC

                                    MainPlaylistController.sort()
                                }
                            }
                        }
                    }

                    // Title is special and is always shown
                    SplitViewDelegate {
                        id: titleItem

                        objectName: "title"
                        text: qsTr("Title")
                        sortKey: PlaylistController.SORT_KEY_TITLE

                        T.SplitView.preferredWidth: VLCStyle.dp(256, VLCStyle.scale) // Default
                        T.SplitView.fillWidth: (headerRepeater.count === 0)
                    }

                    Repeater {
                        id: headerRepeater

                        model: headerMap.selectedItems

                        SplitViewDelegate {
                            objectName: modelData

                            required property string modelData

                            text: headerMap[modelData].actionText
                        }
                    }
                }
            }
        }

        Widgets.ListViewExt {
            id: listView

            Layout.fillWidth: true
            Layout.fillHeight: true

            selectionModel: ListSelectionModel {
                model: listView.model
            }

            focus: true

            clip: !fadingEdge.implicitClipping && (height < contentHeight)

            model: proxyModel ?? root.model

            onWidthChanged: {
                if (searchBox.expanded) {
                    if (width < searchBox.textField.width)
                        searchBox.retract()
                }
            }

            // Disable the add transition in detailed case, Qt transitions
            // are bugged and with the detailed delegate the bugs are exposed:
            Binding on add {
                when: root.detailed
                value: null
            }

            property QtSortFilterProxyModel proxyModel

            function adjustFiltering() {
                // Once text changes, we switch to the proxy model.
                // We do not switch back to the source model after that.
                if (!proxyModel) {
                    proxyModel = proxyModelComponent.createObject(listView)
                }

                if (searchBox.regexButtonToggled)
                    proxyModel.setFilterRegularExpression(searchBox.searchPattern)
                else
                    proxyModel.setFilterFixedString(searchBox.searchPattern)
            }

            Component {
                id: proxyModelComponent

                QtSortFilterProxyModel {
                    filterRole: PlaylistListModel.FilterRole

                    sourceModel: root.model

                    filterCaseSensitivity: searchBox.caseSensitiveButtonToggled ? Qt.CaseSensitive
                                                                                : Qt.CaseInsensitive
                }
            }

            fadingEdge.backgroundColor: (root.background && (root.background.color.a >= 1.0)) ? root.background.color
                                                                                              : Qt.alpha(root.background.color, 0.0)

            function mappedSelectedIndexes() : var {
                let mappedSelectedIndexes

                if (!listView.selectionModel)
                    return [] // unlikely

                const selectedIndexes = listView.selectionModel.selectedIndexesFlat
                if (listView.proxyModel && listView.model === listView.proxyModel) {
                    mappedSelectedIndexes = []
                    for (let i = 0; i < selectedIndexes.length; ++i) {
                        const mappedIndex = listView.model.mapToSource(listView.model.index(selectedIndexes[i], 0))
                        mappedSelectedIndexes.push(mappedIndex.row)
                    }
                } else {
                    mappedSelectedIndexes = selectedIndexes
                }

                return mappedSelectedIndexes
            }

            isDropAcceptableFunc: function(drop, index) {
                if (drop.source === dragItem)
                    return Helpers.itemsMovable(listView.mappedSelectedIndexes(), index)
                else if (Helpers.isValidInstanceOf(drop.source, Widgets.DragItem))
                    return true
                else if (drop.hasUrls)
                    return true
                else
                    return false
            }

            acceptDropFunc: function(index, drop) {
                const item = drop.source;

                // NOTE: Move implementation.
                if (dragItem === item) {
                    root.model.moveItemsPre(listView.mappedSelectedIndexes(), index);
                    listView.forceActiveFocus();
                // NOTE: Dropping medialibrary content into the queue.
                } else if (Helpers.isValidInstanceOf(item, Widgets.DragItem)) {
                    return item.getSelectedInputItem().then((inputItems) => {
                            if (!Helpers.isArray(inputItems) || inputItems.length === 0) {
                                console.warn("can't convert items to input items");
                                return
                            }
                            MainPlaylistController.insert(index, inputItems, false)
                        }).then(() => { listView.forceActiveFocus(); })
                // NOTE: Dropping an external item (i.e. filesystem) into the queue.
                } else if (drop.hasUrls) {
                    const urlList = [];

                    for (let url in drop.urls)
                        urlList.push(drop.urls[url]);

                    MainPlaylistController.insert(index, urlList, false);

                    // NOTE This is required otherwise backend may handle the drop as well yielding double addition.
                    drop.accept(Qt.IgnoreAction);
                    listView.forceActiveFocus();
                }

                return Promise.resolve()
            }

            property int shiftIndex: -1

            onShowContextMenu: (globalPos) => {
                contextMenu.popup(-1, globalPos)
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
                // WARNING: Tracking the current item and not the current index is intentional here.
                MainPlaylistController.currentItemChanged.connect(listView, () => {
                    // FIXME: Qt does not provide the `contentY` with `positionViewAtIndex()` for us
                    //        to animate. For that reason, we capture the new `contentY`, adjust
                    //        `contentY` to it is old value then enable the animation and set `contentY`
                    //        to its new value.
                    const oldContentY = listView.contentY
                    listView.positionViewAtIndex(MainPlaylistController.currentIndex, ListView.Contain)
                    const newContentY = listView.contentY
                    if (Math.abs(oldContentY - newContentY) >= Number.EPSILON) {
                        contentYBehavior.enabled = false
                        listView.contentY = oldContentY
                        contentYBehavior.enabled = true
                        listView.contentY = newContentY
                        contentYBehavior.enabled = false
                    }
                })

                searchBox.searchPatternChanged.connect(listView, listView.adjustFiltering)
                searchBox.regexButtonToggledChanged.connect(listView, listView.adjustFiltering)
            }

            Connections {
                target: listView.model

                function onRowsInserted() {
                    if (listView.currentIndex === -1)
                        listView.currentIndex = 0
                }

                function onModelReset() {
                    if (listView.currentIndex === -1 && listView.count > 0)
                        listView.currentIndex = 0
                }
            }

            delegate: root.detailed ? detailedDelegateComponent : normalDelegateComponent

            Component {
                id: normalDelegateComponent

                PlaylistDelegate {
                    id: delegate

                    width: listView.contentWidth
                    rightPadding: Math.max(listView.ScrollBar.vertical.width, VLCStyle.margin_normal)

                    contextMenu: root.contextMenu

                    dragItem: root.dragItem

                    isDropAcceptable: listView.isDropAcceptableFunc
                    acceptDrop: listView.acceptDropFunc

                    onContainsDragChanged: listView.updateItemContainsDrag(this, containsDrag)

                    onIsCurrentChanged: {
                        if (isCurrent)
                            listView.fadingEdge.excludeItem = delegate
                        else if (listView.fadingEdge.excludeItem === delegate)
                            listView.fadingEdge.excludeItem = null
                    }
                }
            }

            Component {
                id: detailedDelegateComponent

                PlaylistDetailedDelegate {
                    id: delegate

                    width: listView.contentWidth
                    rightPadding: Math.max(listView.ScrollBar.vertical.width, VLCStyle.margin_normal)

                    verticalPadding: VLCStyle.margin_xxsmall

                    contextMenu: root.contextMenu

                    dragItem: root.dragItem

                    isDropAcceptable: listView.isDropAcceptableFunc
                    acceptDrop: listView.acceptDropFunc

                    readonly property T.Container header: (headerLoader.item instanceof T.Container) ? headerLoader.item : null
                    // FIXME: Qt 6.2.13 can not use `list<Item>` here:
                    readonly property var /*list<Item>*/ headerItemMap: header?.headerItemMap ?? null

                    contentTextHorizontalPadding: header?.contentTextHorizontalPadding ?? 0.0

                    titleWidth: (header?.titleItem?.width + (header?.handleWidth ?? 0.0)) ?? VLCStyle.dp(256, VLCStyle.scale) // Title is always visible
                    durationWidth: headerItemMap?.duration?.width ? (headerItemMap.duration.width + header?.handleWidth ?? 0.0) : 0.0
                    artistWidth: headerItemMap?.artist?.width ? (headerItemMap.artist.width + header?.handleWidth ?? 0.0) : 0.0
                    genreWidth: headerItemMap?.genre?.width ? (headerItemMap.genre.width + header?.handleWidth ?? 0.0) : 0.0
                    albumWidth: headerItemMap?.album?.width ? (headerItemMap.album.width + header?.handleWidth ?? 0.0) : 0.0
                    urlWidth: headerItemMap?.url?.width ? (headerItemMap.url.width + header?.handleWidth ?? 0.0) : 0.0
                    trackNumberWidth: headerItemMap?.trackNumber?.width ? (headerItemMap.trackNumber.width + header?.handleWidth ?? 0.0) : 0.0
                    descriptionWidth: headerItemMap?.description?.width ? (headerItemMap.description.width + header?.handleWidth ?? 0.0) : 0.0
                    trackIdWidth: headerItemMap?.trackId?.width ? (headerItemMap.trackId.width + header?.handleWidth ?? 0.0) : 0.0
                    ratingWidth: headerItemMap?.rating?.width ? (headerItemMap.rating.width + header?.handleWidth ?? 0.0) : 0.0
                    discNumberWidth: headerItemMap?.discNumber?.width ? (headerItemMap.discNumber.width + header?.handleWidth ?? 0.0) : 0.0
                    dateWidth: headerItemMap?.date?.width ? (headerItemMap.date.width + header?.handleWidth ?? 0.0) : 0.0

                    onContainsDragChanged: listView.updateItemContainsDrag(this, containsDrag)

                    onIsCurrentChanged: {
                        if (isCurrent)
                            listView.fadingEdge.excludeItem = delegate
                        else if (listView.fadingEdge.excludeItem === delegate)
                            listView.fadingEdge.excludeItem = null
                    }
                }
            }

            Keys.onDeletePressed: {
                let items

                const selectedIndexes = selectionModel.selectedIndexesFlat
                if (listView.proxyModel && listView.model === listView.proxyModel) {
                    items = []
                    for (let i = 0; i < selectedIndexes.length; ++i) {
                        const mappedIndex = listView.proxyModel.mapToSource(listView.proxyModel.index(selectedIndexes[i], 0))
                        items.push(mappedIndex.row)
                    }
                } else {
                    items = selectedIndexes
                }

                if (items.length > 0)
                    root.model.removeItems(items)
            }

            Navigation.parentItem: root
            Navigation.upItem: sort

            onActionAtIndex: (index) => {
                if (index < 0)
                    return

                MainPlaylistController.goTo(index, true)
            }

            Column {
                id: noContentInfoColumn

                anchors.centerIn: parent

                visible: false
                enabled: visible

                opacity: (listView.activeFocus) ? 1.0 : 0.4

                Binding on visible {
                    delayed: true
                    value: (listView.count === 0 && !listView.footerItem.firstItemIndicatorVisible)
                }

                Widgets.IconLabel {
                    id: label

                    anchors.horizontalCenter: parent.horizontalCenter

                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter

                    text: VLCIcons.playlist

                    color: theme.fg.primary

                    font.pixelSize: VLCStyle.dp(48, VLCStyle.scale)
                }

                T.Label {
                    anchors.topMargin: VLCStyle.margin_xlarge

                    anchors.horizontalCenter: parent.horizontalCenter

                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter

                    text: (listView.proxyModel &&
                           listView.model === listView.proxyModel &&
                           listView.count !== root.model.count) ? qsTr("No results")
                                                                : qsTr("No content yet")

                    color: label.color

                    font.pixelSize: VLCStyle.fontSize_xxlarge
                }

                T.Label {
                    anchors.topMargin: VLCStyle.margin_normal

                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter

                    text: qsTr("Drag & Drop some content here!")
                    visible: (listView.proxyModel && listView.model === listView.proxyModel) ? (listView.count === root.model.count)
                                                                                             : true

                    color: label.color

                    font.pixelSize: VLCStyle.fontSize_large
                }
            }
        }
    }

    Keys.priority: Keys.AfterItem
    Keys.forwardTo: listView
    Keys.onPressed: (event) => root.Navigation.defaultKeyAction(event)

    component PlaylistToggleButton : Widgets.IconToolButton {
        id: playlistBtn

        focus: true

        font.pixelSize: VLCStyle.icon_banner
        text: VLCIcons.playlist
        description: qsTr("Play queue")

        AccessibleCompat.id: "playqueueToggle"

        width: VLCStyle.bannerButton_width
        height: VLCStyle.bannerButton_height
        //highlighted: MainCtx.playlistVisible

        property Item plListView

        DropArea {
            anchors.fill: parent

            onContainsDragChanged: {
                if (containsDrag) {
                    _timer.restart()

                    if (playlistBtn.plListView)
                        MainCtx.setCursor(Qt.DragCopyCursor)
                } else {
                    _timer.stop()

                    if (playlistBtn.plListView)
                        MainCtx.restoreCursor()
                }
            }

            onEntered: (drag) => {
                const plListView = playlistBtn.plListView
                if (plListView) {
                    console.assert(plListView.isDropAcceptableFunc)
                    console.assert(plListView.model)
                    if (plListView.isDropAcceptableFunc(drag, plListView.model.count)) {
                        drag.accept()
                    } else {
                        drag.accepted = false
                    }
                } else {
                    drag.accepted = false
                }
            }

            onDropped: (drop) => {
                const plListView = playlistBtn.plListView
                if (plListView) {
                    console.assert(plListView.acceptDropFunc)
                    plListView.acceptDropFunc(plListView.model.count, drop)
                }
            }

            Timer {
                id: _timer
                interval: VLCStyle.duration_humanMoment

                onTriggered: {
                    MainCtx.playqueuePanel.visible = true
                }
            }
        }
    }
}
