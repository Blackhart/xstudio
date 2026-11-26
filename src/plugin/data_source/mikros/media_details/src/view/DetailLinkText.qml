// SPDX-License-Identifier: Apache-2.0
import QtQuick

import xStudio 1.0

// Accent text that opens a ShotGrid entity page when clicked.
Text {
    id: link

    property string entityType: ""
    property var entityId: 0

    signal entityClicked(string entityType, var entityId)

    readonly property bool canLink: entityType !== "" && Number(entityId) > 0

    color: canLink ? XsStyleSheet.accentColor : XsStyleSheet.primaryTextColor
    font.pixelSize: XsStyleSheet.fontSize
    font.family: XsStyleSheet.fontFamily
    font.underline: canLink
    elide: Text.ElideRight

    MouseArea {
        anchors.fill: parent
        enabled: link.canLink
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: link.entityClicked(link.entityType, link.entityId)
    }
}
