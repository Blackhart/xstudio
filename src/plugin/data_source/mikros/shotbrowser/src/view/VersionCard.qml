// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts

import xStudio 1.0

// One version row: name, author, statuses, the cached image, and the open gesture.
Rectangle {
    id: card

    // Version list and playlist: JSON object for one card (ListView injects it when required).
    required property var modelData

    property bool chosen: false
    property int indent: 0

    signal openCurrent()
    signal menuRequested(real x, real y)

    function rowBag() {
        if (modelData !== undefined && modelData !== null && typeof modelData === "object")
            return modelData
        return null
    }

    function field(key) {
        const row = rowBag()
        if (!row || row[key] === undefined || row[key] === null)
            return ""
        return String(row[key])
    }

    function flag(key) {
        const row = rowBag()
        if (!row || row[key] === undefined || row[key] === null)
            return false
        return row[key] === true || row[key] === 1 || row[key] === "1"
    }

    function shown(value) {
        return value === "" ? "N/A" : value
    }

    function siteOn(code) {
        const cities = {prs: "Paris", mtl: "Montreal", bgl: "Bangalore"}
        const city = cities[code]
        if (!city || sites === "")
            return false
        const names = sites.split(",")
        for (let i = 0; i < names.length; i++) {
            if (names[i].trim() === city)
                return true
        }
        return false
    }

    function payload() {
        return {
            versionId: versionId,
            label: label,
            frameRange: frameRange
        }
    }

    readonly property string label: field("name")
    readonly property string imageSource: {
        const value = field("image")
        return value
    }
    readonly property string author: field("author")
    readonly property string createdAt: field("createdAt")
    readonly property string frameRange: field("frameRange")
    readonly property string description: field("description")
    readonly property string stepName: field("step")
    readonly property string stepStatus: field("status")
    readonly property string artisticStatus: field("artisticStatus")
    readonly property string sites: field("sites")
    readonly property bool hasNotes: flag("hasNotes")
    readonly property bool syncedToClient: flag("syncedToClient")
    readonly property int versionId: {
        const row = rowBag()
        if (!row || row.id === undefined)
            return 0
        return Number(row.id)
    }

    x: indent
    width: (ListView.view ? ListView.view.width : parent.width) - x
    height: (XsStyleSheet.widgetStdHeight + 1) * 4
    color: chosen ? Qt.darker(XsStyleSheet.accentColor, 5) : "transparent"
    border.width: 1
    border.color: rowHover.hovered || versionPress.containsMouse
                  ? XsStyleSheet.accentColor
                  : XsStyleSheet.widgetBgNormalColor

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 1

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: XsStyleSheet.widgetStdHeight
            Layout.maximumHeight: XsStyleSheet.widgetStdHeight
            color: XsStyleSheet.widgetBgNormalColor

            XsText {
                anchors.fill: parent
                text: card.label
                font.pixelSize: XsStyleSheet.fontSize * 1.1
                font.bold: true
                leftPadding: XsStyleSheet.panelPadding
                rightPadding: XsStyleSheet.panelPadding
                horizontalAlignment: Text.AlignLeft
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 1

            RowLayout {
                Layout.preferredWidth: 154
                Layout.minimumWidth: 154
                Layout.maximumWidth: 154
                Layout.fillHeight: true
                spacing: 1

                ColumnLayout {
                    Layout.preferredWidth: 20
                    Layout.maximumWidth: 20
                    Layout.fillHeight: true
                    spacing: 1

                    Repeater {
                        model: [
                            {letter: "N", on: card.hasNotes},
                            {letter: "C", on: card.syncedToClient}
                        ]

                        XsPrimaryButton {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            text: modelData.letter
                            font.pixelSize: XsStyleSheet.fontSize * 1.2
                            textDiv.color: modelData.on
                                          ? XsStyleSheet.accentColor
                                          : XsStyleSheet.hintColor
                            enabled: true
                            bgDiv.opacity: modelData.on ? 1 : 0.5
                            isUnClickable: true
                            isActiveViaIndicator: modelData.on
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "black"

                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: Math.min(width * 9 / 16, parent.height)
                        source: card.imageSource !== ""
                                ? card.imageSource
                                : "qrc:/mikros_shotbrowser_icons/no_preview.jpg"
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 115
                spacing: 1

                XsText {
                    Layout.fillWidth: true
                    Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                    Layout.maximumHeight: XsStyleSheet.widgetStdHeight
                    text: card.shown(card.author)
                    leftPadding: XsStyleSheet.panelPadding
                    color: card.author === ""
                           ? XsStyleSheet.hintColor
                           : XsStyleSheet.secondaryTextColor
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                    Layout.maximumHeight: XsStyleSheet.widgetStdHeight
                    spacing: 1

                    XsText {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: card.shown(card.createdAt)
                        leftPadding: XsStyleSheet.panelPadding
                        color: card.createdAt === ""
                               ? XsStyleSheet.hintColor
                               : XsStyleSheet.secondaryTextColor
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }

                    XsText {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: card.shown(card.frameRange)
                        leftPadding: XsStyleSheet.panelPadding
                        color: card.frameRange === ""
                               ? XsStyleSheet.hintColor
                               : XsStyleSheet.secondaryTextColor
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: XsStyleSheet.widgetBgNormalColor

                    XsText {
                        anchors.fill: parent
                        anchors.margins: XsStyleSheet.panelPadding
                        text: card.shown(card.description)
                        color: card.description === ""
                               ? XsStyleSheet.hintColor
                               : XsStyleSheet.secondaryTextColor
                        wrapMode: Text.WordWrap
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignTop
                    }
                }
            }

            ColumnLayout {
                Layout.preferredWidth: 157
                Layout.minimumWidth: 157
                Layout.maximumWidth: 157
                Layout.fillHeight: true
                spacing: 1

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                    Layout.maximumHeight: XsStyleSheet.widgetStdHeight
                    spacing: 1

                    XsText {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: card.shown(card.stepName)
                        color: card.stepName === ""
                               ? XsStyleSheet.hintColor
                               : XsStyleSheet.secondaryTextColor
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }

                    XsText {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: card.shown(card.stepStatus)
                        color: card.stepStatus === ""
                               ? XsStyleSheet.hintColor
                               : XsStyleSheet.secondaryTextColor
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                XsText {
                    Layout.fillWidth: true
                    Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                    Layout.maximumHeight: XsStyleSheet.widgetStdHeight
                    text: card.shown(card.artisticStatus)
                    color: card.artisticStatus === ""
                           ? XsStyleSheet.hintColor
                           : XsStyleSheet.secondaryTextColor
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: XsStyleSheet.widgetStdHeight - 1
                    Layout.maximumHeight: XsStyleSheet.widgetStdHeight - 1
                    spacing: 1

                    Repeater {
                        model: ["prs", "mtl", "bgl"]

                        Rectangle {
                            required property string modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: card.siteOn(modelData)
                                   ? XsStyleSheet.accentColor
                                   : Qt.lighter(XsStyleSheet.panelBgColor, 1.5)
                            opacity: card.siteOn(modelData) ? 1 : 0.5

                            XsText {
                                anchors.fill: parent
                                text: modelData
                                color: card.siteOn(modelData)
                                       ? "white"
                                       : XsStyleSheet.hintColor
                                font.pixelSize: XsStyleSheet.fontSize / 1.4
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }
        }
    }

    HoverHandler {
        id: rowHover
    }

    MouseArea {
        id: versionPress
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onDoubleClicked: card.openCurrent()
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton)
                card.menuRequested(mouse.x, mouse.y)
        }
    }
}
