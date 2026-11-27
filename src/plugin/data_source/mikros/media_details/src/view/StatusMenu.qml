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
    property real fittedHeight: 280 ///< Height chosen so the list stays inside the overlay.

    function openMenu() {
        if (!editable || busy)
            return
        placePopup()
        statusPopup.open()
    }

    // A badge on the right or the bottom would push the list past the window.
    function placePopup() {
        const margin = 8
        const gap = 4
        const overlay = Overlay.overlay
        const origin = badge.mapToItem(overlay, 0, 0)
        if (!origin)
            return
        const wanted = Math.min(statusColumn.implicitHeight + 8, 280)
        const roomBelow = overlay.height - margin - (origin.y + badge.height + gap)
        const roomAbove = origin.y - gap - margin
        const openBelow = roomBelow >= wanted || roomBelow >= roomAbove
        const room = Math.max(0, openBelow ? roomBelow : roomAbove)
        fittedHeight = Math.min(wanted, room > 0 ? room : wanted, Math.max(0, overlay.height - margin * 2))
        let x = origin.x
        if (x + statusPopup.width > overlay.width - margin)
            x = origin.x + badge.width - statusPopup.width
        statusPopup.x = Math.max(margin, Math.min(x, overlay.width - margin - statusPopup.width))
        statusPopup.y = openBelow
            ? origin.y + badge.height + gap
            : Math.max(margin, origin.y - gap - fittedHeight)
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
        height: menu.fittedHeight

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
