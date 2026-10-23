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
import QtQuick.Layouts
import QtQuick.Templates as T

import VLC.MainInterface
import VLC.Widgets as Widgets
import VLC.Util
import VLC.Style
import VLC.Player

T.Pane {
    id: root

    property bool paintOnly: false

    readonly property color sliderColor: {
        const theme = volControl.colorContext
        return (volControl.position > _fullvolpos) ? theme.fg.negative : theme.fg.primary
    }

    readonly property var _player: paintOnly ? ({ muted: false, volume: .5 }) : Player
    readonly property int _maxvol: MainCtx.maxVolume
    readonly property real _fullvolpos: 100 / _maxvol
    readonly property real _maxvolpos: _maxvol / 100

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    contentWidth: volumeWidget.implicitWidth
    contentHeight: volumeWidget.implicitHeight

    Keys.priority: Keys.AfterItem
    Keys.onPressed: (event) => Navigation.defaultKeyAction(event)

    RowLayout {
        id: volumeWidget

        anchors.fill: parent
        spacing: 0

        Widgets.IconToolButton{
            id: volumeBtn

            focus: true
            paintOnly: root.paintOnly
            text: {
                if (_player.muted)
                    return VLCIcons.volume_muted
                else if (_player.volume === 0)
                    return VLCIcons.volume_zero
                else if (_player.volume < .33)
                    return VLCIcons.volume_low
                else if (_player.volume <= .66)
                    return VLCIcons.volume_medium
                else
                    return VLCIcons.volume_high
            }
            description: qsTr("Mute")

            AccessibleCompat.id: "muteButton"

            onClicked: Player.muted = !Player.muted

            Accessible.onIncreaseAction: {
                Player.muted = false
                Player.setVolumeUp()
            }

            Accessible.onDecreaseAction: {
                Player.muted = false
                Player.setVolumeDown()
            }

            Navigation.parentItem: root
            Navigation.rightItem: volControl
        }

        Widgets.SliderExt {
            id: volControl

            // FIXME: use VLCStyle
            implicitWidth: VLCStyle.dp(64, VLCStyle.scale)

            // WARNING: Directly setting the insets break handle and background positioning.
            Binding on topInset {
                when: MainCtx.usingTouch
                value: VLCStyle.touchHandlerMargin
            }
            Binding on bottomInset {
                when: MainCtx.usingTouch
                value: VLCStyle.touchHandlerMargin
            }

            Layout.fillHeight: true
            Layout.margins: VLCStyle.margin_xsmall

            property bool _inhibitPlayerVolumeUpdate: false

            // FIXME: Currently we are not updating the ShiftModifier status while dragging. This
            //        could be fixed with a custom Slider based on a MouseArea.
            property bool _shiftPressed: false

            property real _clamp: 0.01

            property bool _keyPressed: false

            property bool filterEvents: false

            color: root.sliderColor

            from: 0
            to: root._maxvolpos

            enabled: !root.paintOnly // disables event handling depending on this

            toolTipFollowsMouse: true
            toolTipTextProvider: function (value) {
                return Math.round(value * 100) + "%"
            }

            Accessible.name: qsTr("Volume")
            AccessibleCompat.id: "volumeSlider"

            handle: null

            // TODO: Implement `PointingToolTipAttached::delay`.
            Timer {
                id: toolTipVisibilityTimer

                interval: VLCStyle.delayToolTipAppear

                onTriggered: {
                    volControl.Widgets.PointingToolTipAttached.visible = true
                }
            }

            readonly property bool _hovered: (hovered || sliderMouseArea.containsMouse)
            on_HoveredChanged: {
                if (_hovered) {
                    if (!_pressed) {
                        toolTipVisibilityTimer.restart()
                    }
                } else {
                    toolTipVisibilityTimer.stop()
                    Widgets.PointingToolTipAttached.visible = false
                }
            }

            readonly property bool _pressed: (pressed || sliderMouseArea.pressed)
            on_PressedChanged: {
                if (_pressed) {
                    toolTipVisibilityTimer.stop()
                    Widgets.PointingToolTipAttached.visible = false
                }
            }

            Widgets.PointingToolTipAttached.onTextChanged: {
                if ((Widgets.PointingToolTipAttached.text !== volumeText.text) && !Widgets.PointingToolTipAttached.visible && _hovered && !_pressed)
                    toolTipVisibilityTimer.restart()
            }

            Widgets.PointingToolTipAttached.visible: false

            background: Image {
                id: backgroundImage

                x: volControl.leftPadding
                y: volControl.topPadding + volControl.availableHeight / 2 - height / 2

                width: volControl.availableWidth
                height: volumeBtn.height * 0.85 // Using `Slider`'s height causes polish loop

                cache: true
                fillMode: Image.Pad
                horizontalAlignment: Image.AlignLeft
                sourceSize: Qt.size(width, height)

                // WARNING: SVG does not support RGBA color.
                readonly property color opaqueBorder: (volControl.colorContext.border.a < 1.0) ? (volControl.colorContext.palette.isDark ? "darkgray"
                                                                                                                                         : "gray")
                                                                                               : volControl.colorContext.border

                source: SVGColorImage.colorize("qrc:///misc/volume-slider-outside.svg")
                                     .color1(opaqueBorder).uri() // foreground

                Widgets.AnimatedBackground {
                    anchors.fill: parent
                    anchors.margins: -VLCStyle.margin_xxsmall

                    visible: (border.color.a > 0.0)
                    enabled: volControl.colorContext.initialized

                    color: "transparent"

                    border.color: volControl.visualFocus ? volControl.colorContext.visualFocus
                                                         : Qt.alpha(volControl.colorContext.visualFocus, 0.0)
                }

                Image {
                    z: -1 // Gradient is below parent

                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left

                    width: (parent.width * volControl.position)

                    clip: (volControl.position < volControl.to)

                    cache: true
                    fillMode: Image.Pad
                    horizontalAlignment: Image.AlignLeft
                    sourceSize: Qt.size(parent.width, height)

                    source: Effects.url(Effects.VolumeSliderGradient, { "maximum": (volControl.to * 100),
                                                                        "sliderColors": MainCtx.volumeSliderColors,
                                                                        "muted": _player.muted })
                }

                Text {
                    id: volumeText

                    font.pixelSize: VLCStyle.fontSize_small
                    textFormat: Text.PlainText

                    x: 0
                    y: -VLCStyle.margin_xxxsmall

                    horizontalAlignment: Text.AlignRight

                    text: {
                        // the real value i.e 'Player.volume' bounds can be different
                        // from bounds of this widget and we want to show the current
                        // volume here, so directly use Player.volume instead of "value"

                        return Math.round(Player.volume * 100) + "%"
                    }

                    color: backgroundImage.opaqueBorder // ###
                }
            }

            Keys.onPressed: (event) => {
                if (KeyHelper.matchOk(event)) {
                    event.accepted = true

                    _keyPressed = true
                } else {
                    Navigation.defaultKeyAction(event)
                }
            }

            Keys.onReleased: (event) => {
                if (_keyPressed === false)
                    return

                _keyPressed = false

                if (KeyHelper.matchOk(event)) {
                    Player.muted = !Player.muted
                }
            }

            function _syncVolumeWithPlayer() {
                volControl._inhibitPlayerVolumeUpdate = true
                volControl.value = _player.volume
                volControl._inhibitPlayerVolumeUpdate = false
            }

            function _adjustPlayerVolume() {
                Player.muted = false

                let value = volControl.value

                // NOTE: We are clamping the value to make it easier to restore the default volume.
                if (_shiftPressed === false) {
                    if (Math.abs(value - 1.0) < _clamp)
                        value = 1.0
                    else
                        _clamp = 0.01
                }

                Player.volume = value

                volControl.value = value
            }

            Component.onCompleted: {
                root.paintOnlyChanged.connect(_syncVolumeWithPlayer)
                volControl._syncVolumeWithPlayer()
            }

            Connections {
                target: Player
                enabled: !paintOnly

                function onVolumeChanged() { volControl._syncVolumeWithPlayer() }
            }

            Navigation.leftItem: volumeBtn
            Navigation.parentItem: root

            Keys.onUpPressed: (event) => {
                Player.muted = false
                Player.setVolumeUp()
            }

            Keys.onDownPressed: (event) => {
                Player.muted = false
                Player.setVolumeDown()
            }

            Keys.priority: Keys.BeforeItem

            onValueChanged: {
                if (paintOnly)
                    return

                if (!volControl._inhibitPlayerVolumeUpdate) {
                    if (filterEvents) {
                        Qt.callLater(volControl._adjustPlayerVolume)
                    } else {
                        volControl._adjustPlayerVolume()
                        filterEvents = true
                    }
                }
            }

            MouseArea {
                id: sliderMouseArea

                anchors.fill: parent

                acceptedButtons: (Qt.LeftButton | Qt.RightButton)

                onPressed: (mouse) => {
                    if (root.paintOnly) {
                        mouse.accepted = true
                        return
                    }

                    volControl._shiftPressed = (mouse.modifiers === Qt.ShiftModifier)

                    if (mouse.button === Qt.LeftButton) {
                        mouse.accepted = false

                        return
                    }

                    volControl.filterEvents = false
                    adjustVolume(mouse)
                }

                onPositionChanged: (mouse) => {
                    if (mouse.buttons & Qt.RightButton)
                        adjustVolume(mouse)
                }

                onWheel: (wheel) => {

                    if ((Math.abs(wheel.pixelDelta.x) % 120 > 0) || (Math.abs(wheel.pixelDelta.y) % 120 > 0)) {
                        let delta = 0
                        if (Math.abs(wheel.pixelDelta.x) > Math.abs(wheel.pixelDelta.y))
                            delta = wheel.pixelDelta.x
                        else
                            delta = wheel.pixelDelta.y

                        if (wheel.inverted)
                            delta = -delta
                        volControl.value += 0.001 * delta
                    }
                    else
                        wheelToVLC.qmlWheelEvent(wheel)

                    wheel.accepted = true
                }

                function adjustVolume(mouse) {
                    mouse.accepted = true

                    const pos = mouse.x * root._maxvolpos / width

                    if (pos < 0.25)
                        volControl.value = 0
                    else if (pos < 0.75)
                        volControl.value = 0.5
                    else if (pos < 1.125)
                        volControl.value = 1
                    else
                        volControl.value = 1.25
                }

                WheelToVLCConverter {
                    id: wheelToVLC

                    function handle(steps: int) {
                        if (steps > 0)
                            Player.setVolumeUp(steps)
                        else
                            Player.setVolumeDown(-steps)
                    }

                    Component.onCompleted: {
                        wheelUpDown.connect(wheelToVLC.handle)
                        wheelLeftRight.connect(wheelToVLC.handle)
                    }
                }
            }
        }
    }
}
