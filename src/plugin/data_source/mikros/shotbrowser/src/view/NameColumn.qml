// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import xStudio 1.0

// The list, its scroll, its placeholder and its footer. The row comes from the screen.
ColumnLayout {
    id: column

    property string title
    property url iconSrc
    property var names
    property string placeholder
    property bool boxedCount: false
    property bool loadedFooter: false
    property string footerNoun: "version"
    property string footerText: ""
    property bool customFooter: false
    property bool resultSwitcher: false
    property string resultMode: "versions"
    property int switcherHeight: XsStyleSheet.widgetStdHeight + 4
    property int switcherSpacing: 2
    property color listColor: "transparent"
    property bool tree: false
    property bool playlistTree: false
    property int footerCount: -1
    property Component rowDelegate

    signal resultChosen(string mode)

    readonly property int count: names && names.length ? names.length : 0
    readonly property int footerValue: footerCount >= 0 ? footerCount : count
    readonly property string footerLabel: customFooter
            ? footerText
            : (footerValue === 0 ? "No " + footerNoun + "s found"
               : footerValue === 1 ? "1 " + footerNoun + " loaded"
               : footerValue + " " + footerNoun + "s loaded")

    onNamesChanged: nameView.holdScroll() ///< A new page must not jump back to the top.

    spacing: XsStyleSheet.panelPadding

    // Row whose height cannot grow with the column.
    component Bar: RowLayout {
        property int barHeight: XsStyleSheet.widgetStdHeight
        spacing: 4
        Layout.fillWidth: true
        Layout.fillHeight: false
        Layout.preferredHeight: barHeight
        Layout.minimumHeight: barHeight
        Layout.maximumHeight: barHeight
    }

    Bar {
        visible: column.title !== "" || column.resultSwitcher
        barHeight: column.resultSwitcher ? column.switcherHeight : XsStyleSheet.widgetStdHeight
        spacing: column.resultSwitcher ? column.switcherSpacing : 1

        XsPrimaryButton {
            visible: column.resultSwitcher
            Layout.fillWidth: visible
            Layout.minimumWidth: 0
            Layout.preferredWidth: 0
            Layout.fillHeight: true
            text: "Versions"
            isActive: column.resultMode === "versions"
            onClicked: column.resultChosen("versions")
        }

        XsPrimaryButton {
            visible: column.resultSwitcher
            Layout.fillWidth: visible
            Layout.minimumWidth: 0
            Layout.preferredWidth: 0
            Layout.fillHeight: true
            text: "Playlist"
            isActive: column.resultMode === "playlist"
            onClicked: column.resultChosen("playlist")
        }

        XsIcon {
            visible: !column.resultSwitcher && column.iconSrc != ""
            Layout.preferredWidth: visible ? 16 : 0
            Layout.preferredHeight: 16
            source: column.iconSrc
        }

        XsText {
            visible: !column.resultSwitcher
            text: column.title
            Layout.fillWidth: visible
            Layout.preferredWidth: visible ? implicitWidth : 0
            font.pixelSize: XsStyleSheet.fontSize * 1.2
            font.weight: Font.Medium
        }

        XsText {
            visible: !column.resultSwitcher && !column.loadedFooter && !column.boxedCount
            Layout.preferredWidth: visible ? implicitWidth : 0
            text: column.count
            opacity: 0.5
            font.pixelSize: XsStyleSheet.fontSize * 1.2
            horizontalAlignment: Text.AlignRight
        }

        Rectangle {
            visible: !column.resultSwitcher && !column.loadedFooter && column.boxedCount
            Layout.preferredWidth: visible ? 72 : 0
            Layout.fillHeight: true
            color: XsStyleSheet.panelBgColor
            border.width: 1
            border.color: XsStyleSheet.menuBorderColor

            XsText {
                anchors.centerIn: parent
                text: column.count
                font.pixelSize: XsStyleSheet.fontSize * 1.2
            }
        }
    }

    Rectangle {
        visible: column.title !== "" || column.resultSwitcher
        Layout.fillWidth: true
        Layout.preferredHeight: 2
        color: XsStyleSheet.panelBgColor
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        color: column.listColor

        XsLabel {
            anchors.fill: parent
            visible: column.count === 0
            text: column.placeholder
            color: XsStyleSheet.hintColor
            font.pixelSize: XsStyleSheet.fontSize * 1.2
            font.weight: Font.Medium
        }

        ListView {
            id: nameView
            anchors.fill: parent
            visible: column.count > 0
            clip: true
            spacing: column.tree && !column.playlistTree ? 1 : XsStyleSheet.panelPadding
            model: column.names
            delegate: column.rowDelegate

            function holdScroll() {
                const y = contentY
                Qt.callLater(function() {
                    const limit = Math.max(0, contentHeight - height)
                    contentY = Math.max(0, Math.min(y, limit))
                })
            }
            // One row per wheel notch, and the flick stops at once.
            flickDeceleration: 100000
            maximumFlickVelocity: 800
            boundsBehavior: Flickable.StopAtBounds

            WheelHandler {
                onWheel: function(event) {
                    const row = XsStyleSheet.widgetStdHeight
                    const step = event.angleDelta.y !== 0
                                 ? (event.angleDelta.y / 120) * row
                                 : Math.max(-row, Math.min(row, event.pixelDelta.y))
                    const limit = Math.max(0, nameView.contentHeight - nameView.height)
                    nameView.contentY = Math.max(0, Math.min(nameView.contentY - step, limit))
                    event.accepted = true
                }
            }

            ScrollBar.vertical: XsScrollBar {
                visible: nameView.height < nameView.contentHeight
                policy: ScrollBar.AsNeeded
            }
        }
    }

    Rectangle {
        visible: column.loadedFooter
        Layout.fillWidth: true
        Layout.preferredHeight: 2
        Layout.maximumHeight: 2
        color: XsStyleSheet.panelBgColor
    }

    XsText {
        visible: column.loadedFooter
        Layout.fillWidth: true
        Layout.fillHeight: false
        Layout.preferredHeight: implicitHeight
        Layout.maximumHeight: implicitHeight
        text: column.footerLabel
        color: "white"
        font.pixelSize: XsStyleSheet.fontSize * 0.8
        horizontalAlignment: Text.AlignRight
        verticalAlignment: Text.AlignVCenter
        rightPadding: XsStyleSheet.panelPadding
    }
}
