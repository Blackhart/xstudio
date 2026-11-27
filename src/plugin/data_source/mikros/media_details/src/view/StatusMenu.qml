// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls.Basic

import xStudio 1.0

// Colored status capsule. A click opens the schema codes.
Item {
    id: menu

    property string code: ""
    property string label: code
    property string tone: ""
    property bool editable: false
    property bool busy: false
    property var codes: []
    property var labelOf: function(key) { return String(key) }
    property var toneOf: function(key) { return "" }

    signal chosen(string code)

    implicitWidth: badge.implicitWidth
    implicitHeight: badge.implicitHeight
    opacity: busy ? 0.55 : 1

    function openMenu() {
        if (!editable || busy)
            return
        const pos = badge.mapToItem(Overlay.overlay, 0, badge.height + 4)
        statusPopup.x = pos.x
        statusPopup.y = pos.y
        statusPopup.open()
    }

    StatusBadge {
        id: badge
        code: menu.code
        label: menu.label
        tone: menu.tone
        showsArrow: menu.editable

        MouseArea {
            anchors.fill: parent
            enabled: menu.editable && !menu.busy
            cursorShape: Qt.PointingHandCursor
            onClicked: menu.openMenu()
        }
    }

    Popup {
        id: statusPopup
        parent: Overlay.overlay
        modal: true
        focus: true
        padding: 4
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width: Math.max(badge.width, 160)
        height: Math.min(statusColumn.implicitHeight + 8, 280)

        background: Rectangle {
            color: XsStyleSheet.panelBgColor
            border.width: 1
            border.color: XsStyleSheet.widgetBgNormalColor
            radius: 6
        }

        contentItem: Flickable {
            contentWidth: width
            contentHeight: statusColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: statusColumn
                width: parent.width
                spacing: 2

                Repeater {
                    model: menu.codes

                    delegate: Item {
                        required property string modelData
                        required property int index

                        width: statusColumn.width
                        height: XsStyleSheet.widgetStdHeight

                        Rectangle {
                            anchors.fill: parent
                            radius: 4
                            color: rowHover.hovered
                                   ? XsStyleSheet.widgetBgNormalColor
                                   : "transparent"
                        }

                        StatusBadge {
                            anchors.left: parent.left
                            anchors.leftMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            code: modelData
                            label: menu.labelOf(modelData)
                            tone: menu.toneOf(modelData)
                        }

                        HoverHandler {
                            id: rowHover
                        }

                        TapHandler {
                            onTapped: {
                                statusPopup.close()
                                if (modelData !== menu.code)
                                    menu.chosen(modelData)
                            }
                        }
                    }
                }
            }
        }
    }
}
