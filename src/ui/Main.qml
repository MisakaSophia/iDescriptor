// SPDX-FileCopyrightText: 2025-2026 Uncore <https://github.com/uncor3>
// SPDX-License-Identifier: AGPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import "."

ApplicationWindow {
    id: window
    title: qsTr("iDescriptor")
    width: 1000
    height: 668
    minimumWidth: 900
    minimumHeight: 550
    visible: true
    color: Theme.windowBackground
    palette: Theme.palette

    Component.onCompleted: {
        Updater.checkAutomatically()
        if (settingsManager.airplay_audio_start_on_launch())
            Qt.callLater(function() { AirplayImp.start_audio() })
    }

    onClosing: function(close) {
        ClosingHandler.handler("*", close, window)
    }

    MainWorkspace {
        anchors.fill: parent
    }
}
