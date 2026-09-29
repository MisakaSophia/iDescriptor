// SPDX-FileCopyrightText: 2025-2026 Uncore <https://github.com/uncor3>
// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import QtMultimedia

import iDescriptor
import org.freedesktop.gstreamer.Qt6GLVideoItem
import ".." as App
import "../base"

ToolWindow {
    id: root

    width: 900
    height: 600
    minimumWidth: 800
    minimumHeight: 560
    visible: true
    title: qsTr("AirPlay - iDescriptor")
    color: App.Theme.controlFill

    property bool serverRunning: false
    property bool clientConnected: false
    property bool tutorialVideoLoaded: false
    property real lastAudibleVolume: 100
    property string clientDeviceName: ""
    property string clientModel: ""
    property string clientDeviceId: ""
    property string parsedModel: ""
    property int receiverPort: 0
    // this is needed because the AirplayImp can only be used
    // to start the audio receiver
    property bool ownsReceiver: false
    readonly property real minimumDisplayScale: 0.5
    readonly property real maximumDisplayScale: 3.0

    function setMasterVolume(volume) {
        const boundedVolume = Math.max(0, Math.min(100, volume))
        volumeSlider.value = boundedVolume
        if (boundedVolume > 0)
            root.lastAudibleVolume = boundedVolume
        AirplayImp.set_master_volume(boundedVolume / 100)
    }

    function toggleMute() {
        if (volumeSlider.value > 0) {
            root.lastAudibleVolume = volumeSlider.value
            root.setMasterVolume(0)
        } else {
            root.setMasterVolume(Math.max(1, root.lastAudibleVolume))
        }
    }

    function setDisplayScale(scale) {
        streamingPage.displayScale = Math.max(root.minimumDisplayScale,
                                              Math.min(root.maximumDisplayScale, scale))
    }

    function zoomDisplay(amount) {
        root.setDisplayScale(streamingPage.displayScale + amount)
    }

    function resetDisplay() {
        streamingPage.rotationTurns = Math.round(streamingPage.rotationTurns / 4) * 4
        root.setDisplayScale(1)
    }

    function startAirPlay() {
        if (AirplayImp.mode === 2 && AirplayImp.audioActive) {
            switchToMirroringDialog.open()
            return
        }
        root.startMirroringBackend()
    }

    function startMirroringBackend() {
        root.serverRunning = false
        stateView.viewState = StateView.State.Loading
        root.startBackend()
    }

    function startBackend() {
        root.ownsReceiver = true
    const started = AirplayImp.init(video)
        if (!started) {
            root.ownsReceiver = false
            stateView.errorText = qsTr("Failed to start AirPlay.")
            stateView.viewState = StateView.State.Error
            return
        }

        root.serverRunning = false
    }


    Component.onCompleted: {
        App.Settings.loadSettings()
        initTimer.start()
    }

    onClosing: {
        tutorialVideo.stop()
        if (root.ownsReceiver)
            AirplayImp.cleanup()
    }

    MessageDialog {
        id: switchToMirroringDialog
        title: qsTr("Stop AirPlay Audio?")
        text: qsTr("Audio is currently playing through iDescriptor. Starting Screen Mirroring will disconnect it.")
        buttons: MessageDialog.Yes | MessageDialog.No
        onButtonClicked: function(button, role) {
            if (button === MessageDialog.Yes)
                root.startMirroringBackend()
            else
                root.close()
        }
    }

    Connections {
        target: AirplayImp

        function onConnectionChange(connected) {
            console.log("AirPlay connection change:", connected)
            root.clientConnected = connected
            if (connected) {
                tutorialVideo.pause()
            } else {
                root.clientDeviceName = ""
                root.clientModel = ""
                root.clientDeviceId = ""
                root.parsedModel = ""
                root.resetDisplay()
                if (root.tutorialVideoLoaded && tutorialPage.visible)
                    tutorialVideo.play()
            }
        }

        function onConnectionDetailsChanged(name, model, parsed_model, device_id) {
            root.clientDeviceName = name
            root.clientModel = model
            root.parsedModel = parsed_model
            root.clientDeviceId = device_id
        }

        function onServerReady(port) {
            root.receiverPort = port
            root.serverRunning = true
            stateView.viewState = StateView.State.Content
            tutorialLoadTimer.start()
        }

        function onBackendFailed(code, detail) {
            root.serverRunning = false
            stateView.errorText = qsTr("Failed to start the AirPlay backend: %1").arg(detail)
            stateView.viewState = StateView.State.Error
        }
    }


    Timer {
        id: initTimer
        interval: 450
        repeat: false
        onTriggered: root.startAirPlay()
    }

    Timer {
        id: tutorialLoadTimer
        interval: 250
        repeat: false
        onTriggered: root.tutorialVideoLoaded = true
    }

    StateView {
        id: stateView
        anchors.fill: parent
        viewState: StateView.State.Loading
        autoSwitchContent: false
        retryable: true
        errorText: qsTr("Failed to start AirPlay.")
        onRetryRequested: {
            viewState = StateView.State.Loading
            initTimer.restart()
        }

        contentItem: StackLayout {
            anchors.fill: parent
            currentIndex: root.clientConnected ? 1 : 0

            Item {
                id: tutorialPage

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            text: root.serverRunning
                                  ? qsTr("Waiting for device connection")
                                  : qsTr("Starting AirPlay Server...")
                            color: palette.text
                            font.pixelSize: 16
                            font.bold: true
                        }

                        BusyIndicator {
                            running: !root.clientConnected
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                        }

                        Button {
                            text: qsTr("Settings")
                            onClicked: App.Settings.open()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: App.Theme.softBg
                        border.color: App.Theme.softBgBorder
                        border.width: 1
                        radius: 10
                        clip: true

                        StackLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            currentIndex: root.tutorialVideoLoaded ? 1 : 0

                            ColumnLayout {
                                spacing: 10

                                Item { Layout.fillHeight: true }

                                BusyIndicator {
                                    Layout.alignment: Qt.AlignHCenter
                                    running: !root.tutorialVideoLoaded
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Loading AirPlay tutorial...")
                                    horizontalAlignment: Text.AlignHCenter
                                    color: palette.text
                                }

                                Item { Layout.fillHeight: true }
                            }

                            Video {
                                id: tutorialVideo
                                fillMode: VideoOutput.PreserveAspectFit
                                source: root.tutorialVideoLoaded ? "qrc:/resources/airplay-tutorial.mp4" : ""
                                loops: MediaPlayer.Infinite
                                muted: true
                                onVisibleChanged: {
                                    if (visible && root.tutorialVideoLoaded && !root.clientConnected)
                                        play()
                                    else
                                        pause()
                                }
                                onSourceChanged: {
                                    if (source && visible && !root.clientConnected)
                                        play()
                                }
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Open Control Center on your device, choose Screen Mirroring, then select iDescriptor.")
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        color: palette.text
                    }
                }
            }

            Item {
                id: streamingPage
                property bool hudVisible: false
                property int rotationTurns: 0
                property real displayScale: 1.0

                onVisibleChanged: {
                    if (visible) {
                        hudVisible = true
                        hideHudTimer.restart()
                    }
                }

                GstGLQt6VideoItem {
                    id: video
                    anchors.fill: parent
                    objectName: "videoItem"
                    transformOrigin: Item.Center
                    rotation: streamingPage.rotationTurns * 90
                    scale: streamingPage.displayScale

                    Behavior on rotation {
                        NumberAnimation {
                            duration: App.Theme.mediumAnimation
                            easing.type: Easing.OutCubic
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                    onPositionChanged: {
                        streamingPage.hudVisible = true
                        hideHudTimer.restart()
                    }
                    onWheel: function (event) {
                        const steps = event.angleDelta.y !== 0
                                      ? event.angleDelta.y / 120
                                      : event.pixelDelta.y / 40
                        if (steps === 0)
                            return

                        root.setDisplayScale(streamingPage.displayScale * Math.pow(1.12, steps))
                        streamingPage.hudVisible = true
                        hideHudTimer.restart()
                        event.accepted = true
                    }
                }

                Rectangle {
                    id: streamingHud
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.margins: 18
                    width: Math.min(parent.width - 36, hudLayout.implicitWidth + 24)
                    height: hudLayout.implicitHeight + 20
                    radius: App.Theme.sidebarCornerRadius
                    color: App.Theme.acrylicSurface
                    border.color: App.Theme.controlStroke
                    border.width: 1
                    opacity: streamingPage.hudVisible || hudMouse.containsMouse ? 1 : 0

                    Behavior on opacity {
                        NumberAnimation {
                            duration: App.Theme.mediumAnimation
                            easing.type: Easing.OutCubic
                        }
                    }

                    MouseArea {
                        id: hudMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: hideHudTimer.restart()
                    }

                    Timer {
                        id: hideHudTimer
                        interval: 5000
                        repeat: false
                        onTriggered: streamingPage.hudVisible = false
                    }

                    RowLayout {
                        id: hudLayout
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        anchors.topMargin: 10
                        anchors.bottomMargin: 10
                        spacing: 8

                        IconToolButton {
                            icon.source: "qrc:/resources/icons/material-symbols_rotate-right.svg"
                            toolTipText: qsTr("Rotate clockwise")
                            onClicked: streamingPage.rotationTurns += 1
                        }

                        IconToolButton {
                            enabled: streamingPage.displayScale > root.minimumDisplayScale
                            icon.source: "qrc:/resources/icons/mdi_magnify-minus.svg"
                            toolTipText: qsTr("Zoom out")
                            onClicked: root.zoomDisplay(-0.25)
                        }

                        IconToolButton {
                            enabled: streamingPage.displayScale < root.maximumDisplayScale
                            icon.source: "qrc:/resources/icons/mdi_magnify-plus.svg"
                            toolTipText: qsTr("Zoom in")
                            onClicked: root.zoomDisplay(0.25)
                        }

                        IconToolButton {
                            enabled: streamingPage.rotationTurns % 4 !== 0
                                     || Math.abs(streamingPage.displayScale - 1) > 0.001
                            icon.source: "qrc:/resources/icons/ic_outline-refresh.svg"
                            toolTipText: qsTr("Reset display")
                            onClicked: root.resetDisplay()
                        }

                        Rectangle {
                            Layout.preferredWidth: 1
                            Layout.preferredHeight: 22
                            color: App.Theme.separator
                        }

                        IconToolButton {
                            icon.source: volumeSlider.value === 0
                                         ? "qrc:/resources/icons/material-symbols_volume-off.svg"
                                         : "qrc:/resources/icons/material-symbols_volume-mute.svg"
                            toolTipText: volumeSlider.value === 0 ? qsTr("Unmute") : qsTr("Mute")
                            onClicked: root.toggleMute()
                        }

                        Slider {
                            id: volumeSlider

                            Layout.preferredWidth: 124
                            from: 0
                            to: 100
                            value: 100
                            stepSize: 1
                            onMoved: root.setMasterVolume(value)

                            background: Rectangle {
                                x: volumeSlider.leftPadding
                                y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                                width: volumeSlider.availableWidth
                                height: 4
                                radius: height / 2
                                color: App.Theme.separator

                                Rectangle {
                                    width: volumeSlider.visualPosition * parent.width
                                    height: parent.height
                                    radius: parent.radius
                                    color: App.Theme.accent
                                }
                            }

                            handle: Rectangle {
                                x: volumeSlider.leftPadding + volumeSlider.visualPosition
                                   * (volumeSlider.availableWidth - width)
                                y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                                implicitWidth: 16
                                implicitHeight: 16
                                radius: width / 2
                                color: App.Theme.controlFill
                                border.color: volumeSlider.hovered
                                              ? App.Theme.accent
                                              : App.Theme.controlStroke
                                border.width: 1

                                Behavior on border.color {
                                    ColorAnimation { duration: App.Theme.fastAnimation }
                                }
                            }

                            ToolTip.visible: hovered || pressed
                            ToolTip.text: qsTr("Volume: %1%").arg(Math.round(value))
                        }

                        Rectangle {
                            Layout.preferredWidth: 1
                            Layout.preferredHeight: 22
                            color: App.Theme.separator
                        }

                        IconToolButton {
                            icon.source: "qrc:/resources/icons/material-symbols_info-outline.svg"
                            toolTipText: qsTr("Connection information")
                            onClicked: connectionInfoDialog.open()
                        }
                    }
                }
            }
        }
    }

    AnimatedDialog {
        id: connectionInfoDialog

        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(520, parent ? parent.width - 40 : 520)
        modal: true
        focus: true
        title: qsTr("AirPlay Connection")
        standardButtons: Dialog.Close
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        contentItem: ColumnLayout {
            spacing: 0

            Repeater {
                model: [
                    {
                        "label": qsTr("Receiver port"),
                        "value": root.receiverPort > 0 ? root.receiverPort.toString() : qsTr("Starting…")
                    },
                    {
                        "label": qsTr("Device name"),
                        "value": root.clientDeviceName
                    },
                    {
                        "label": qsTr("Model"),
                        "value": `${root.parsedModel} (${root.clientModel})`
                    },
                    {
                        "label": qsTr("Device ID"),
                        "value": root.clientDeviceId
                    }
                ]

                delegate: Rectangle {
                    required property var modelData

                    Layout.fillWidth: true
                    implicitHeight: informationRow.implicitHeight + 24
                    color: "transparent"
                    radius: App.Theme.sidebarCornerRadius

                    RowLayout {
                        id: informationRow

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 18

                        Label {
                            Layout.preferredWidth: 128
                            text: modelData.label
                            color: App.Theme.textMuted
                            font.pixelSize: 13
                        }

                        Label {
                            Layout.fillWidth: true
                            text: modelData.value
                            color: App.Theme.text
                            font.pixelSize: 13
                            textFormat: Text.PlainText
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
        }
    }
}
