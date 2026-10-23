/*****************************************************************************
 * Copyright (C) 2020 VLC authors and VideoLAN
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
import QtQuick.Layouts


import VLC.Widgets as Widgets
import VLC.Util
import VLC.MainInterface
import VLC.Style
import VLC.Network
import VLC.Dialogs
import VLC.Menus

Widgets.PageExt {
    id: root

    title: qsTr("Services")

    signal browseServiceManage(int reason)
    signal browseSourceRoot(string sourceName, int reason)

    hasGridListMode: false
    isSearchable: true

    function positionContentAtBeginning() {
        grid.positionViewAtBeginningAnimated()
    }

    Widgets.ExpandGridItemView {
        //settings
        id: grid

        anchors.fill: parent

        focus: true

        basePictureWidth: VLCStyle.gridCover_network_width
        basePictureHeight: VLCStyle.gridCover_network_height
        subtitleHeight: 0

    model: sourcesModel

    headerDelegate: Widgets.ViewHeader {
        view: root

        text: qsTr("Services")
    }

    delegate: Widgets.GridItem {

        property var model: ({})
        property int index: -1
        readonly property bool is_dummy: model.type === NetworkSourcesModel.TYPE_DUMMY
        readonly property bool is_configurable_service: !is_dummy && (model.capabilities & NetworkSourcesModel.SD_CAP_CONFIGURABLE)

            width: grid.cellWidth;
            height: grid.cellHeight;

            pictureWidth: grid.maxPictureWidth
            pictureHeight: grid.maxPictureHeight

            title: is_dummy ? qsTr("Add a service") : model.long_name
            subtitle: ""
            playCoverShowPlay: false
            image: {
                if (is_dummy) {
                    return SVGColorImage.colorize("qrc:///placeholder/add_service.svg")
                        .color1(this.colorContext.fg.secondary)
                        .accent(this.colorContext.accent)
                        .uri()
                } else if (model.artwork && model.artwork.toString() !== "") {
                    //if the source is a qrc artwork, we should colorize it
                    if (model.artwork.toString().match(/qrc:\/\/.*svg/))
                    {
                        return SVGColorImage.colorize(model.artwork)
                            .color1(this.colorContext.fg.secondary)
                            .accent(this.colorContext.accent)
                            .uri()
                    }

                    return model.artwork
                }

                // use fallbackImage
                return ""
            }

            fallbackImage: {
                return SVGColorImage.colorize("qrc:///sd/directory.svg")
                    .color1(this.colorContext.fg.secondary)
                    .uri()
            }

            onItemDoubleClicked: {
                if (is_dummy)
                    root.browseServiceManage(Qt.MouseFocusReason)
                else
                    root.browseSourceRoot(model.name, Qt.TabFocusReason)
            }

            onItemClicked : (modifier, select) => {
                if (select)
                    grid.selectionModel.updateSelection(modifier, grid.currentIndex, index)
                grid.currentIndex = index
                grid.currentItem.forceActiveFocus(Qt.MouseFocusReason)
            }

        onContextMenuButtonClicked: (menuParent, globalMousePos) => {
            if (is_configurable_service) {
                serviceContextMenu.sourceName = model.name
                serviceContextMenu.sourceTitle = model.long_name
                serviceContextMenu.popup(globalMousePos, [qsTr("Configure...")])
            }
        }
    }

        onActionAtIndex: (index) => {
            const itemData = sourcesModel.getDataAt(index);

            if (itemData.type === NetworkSourcesModel.TYPE_DUMMY)
                browseServiceManage(Qt.TabFocusReason)
            else
                browseSourceRoot(itemData.name, Qt.TabFocusReason)
        }

        Navigation.cancelAction: function() {
            History.previous(Qt.BacktabFocusReason)
        }

        NetworkSourcesModel {
            id: sourcesModel

            ctx: MainCtx

            searchPattern: root.search.pattern
            sortOrder: root.sort.order
            sortCriteria: root.sort.criteria
        }
    }

    StringListMenu {
        id: serviceContextMenu

        property string sourceName: ""
        property string sourceTitle: ""

        ctx: MainCtx

        onSelected: (index, modelData) => {
            if (index === 0)
                DialogsProvider.serviceConfigureDialog(sourceName, sourceTitle)
        }
    }
}
