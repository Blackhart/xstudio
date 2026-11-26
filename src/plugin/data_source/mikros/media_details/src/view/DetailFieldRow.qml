// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import xStudio 1.0

// One label/value row; optional ShotGrid link and copy button.
RowLayout {
    id: row

    property string label: ""
    property string value: ""
    property bool show: value !== ""
    property bool copyable: false
    property string entityType: ""
    property var entityId: 0

    signal entityClicked(string entityType, var entityId)
    signal copyRequested(string text)

    Layout.fillWidth: true
    spacing: 8
    visible: show

    Text {
        Layout.preferredWidth: 88
        text: row.label
        color: XsStyleSheet.secondaryTextColor
        font.pixelSize: XsStyleSheet.fontSize
        font.family: XsStyleSheet.fontFamily
    }

    DetailLinkText {
        Layout.fillWidth: true
        text: row.value
        entityType: row.entityType
        entityId: row.entityId
        elide: row.copyable ? Text.ElideMiddle : Text.ElideRight
        onEntityClicked: function(type, id) { row.entityClicked(type, id) }

        ToolTip.visible: row.copyable && valueHover.hovered
        ToolTip.delay: 400
        ToolTip.text: row.value

        HoverHandler {
            id: valueHover
            enabled: row.copyable
        }
    }

    XsPrimaryButton {
        visible: row.copyable && row.value !== ""
        Layout.preferredWidth: 48
        Layout.preferredHeight: XsStyleSheet.widgetStdHeight - 4
        text: "Copy"
        onClicked: row.copyRequested(row.value)
    }
}
