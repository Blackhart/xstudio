// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import xStudio 1.0

// Notes of the version on screen. Colours come from StatusColors.
Item {
    id: root

    property var notesJson: ({})
    property string notesState: "empty"
    property string notesError: ""
    property var statusColors: ({})
    property var statusLabels: ({})
    property string statusDefaultColor: ""
    property string shotgridBaseUrl: ""
    property var onShotgridLink: null
    property string previewSource: ""
    property string previewName: ""
    property bool noteStatusEditable: false
    property bool noteStatusBusy: false
    property string noteStatusError: ""
    property var onNoteStatusEdit: null

    function noteCodes() {
        if (!statusLabels)
            return []
        return Object.keys(statusLabels)
    }

    readonly property var noteRows: root.asList(notesJson && notesJson.notes)

    // A JsonAttribute array arrives as a QVariantList. Array.isArray is false on it.
    function asList(value) {
        if (!value)
            return []
        return value.length ? value : []
    }

    function textOrEmpty(value) {
        if (value === undefined || value === null)
            return ""
        return String(value)
    }

    function people(value) {
        return textOrEmpty(value).split(",").map(function(part) {
            return part.trim()
        }).filter(function(part) {
            return part !== ""
        }).join(", ")
    }

    function whenLabel(value) {
        return textOrEmpty(value).replace("T", " ").replace("Z", "")
    }

    function colorFor(code) {
        const key = textOrEmpty(code)
        if (statusColors && statusColors[key])
            return String(statusColors[key])
        if (statusDefaultColor)
            return String(statusDefaultColor)
        return "#546e7a"
    }

    function labelFor(code) {
        const key = textOrEmpty(code)
        if (statusLabels && statusLabels[key])
            return String(statusLabels[key])
        return key
    }

    function imageSource(path) {
        const url = textOrEmpty(path)
        if (url === "")
            return ""
        if (url.startsWith("file:") || url.startsWith("http:") || url.startsWith("https:") || url.startsWith("qrc:"))
            return url
        if (url.startsWith("/"))
            return "file://" + url
        return ""
    }

    function shotgridUrl(entityId) {
        if (!shotgridBaseUrl || !entityId)
            return ""
        let base = String(shotgridBaseUrl)
        if (base.endsWith("/"))
            base = base.slice(0, -1)
        return base + "/detail/Note/" + String(entityId)
    }

    function openNote(entityId) {
        const url = root.shotgridUrl(entityId)
        if (!url)
            return
        if (onShotgridLink)
            onShotgridLink(url)
    }

    function openAttachment(source, name) {
        if (!source)
            return
        previewSource = source
        previewName = name || ""
        previewPopup.open()
    }

    Text {
        anchors.centerIn: parent
        visible: notesState === "loading"
        text: "Loading…"
        color: XsStyleSheet.secondaryTextColor
        font.pixelSize: XsStyleSheet.fontSize
        font.family: XsStyleSheet.fontFamily
    }

    Text {
        anchors.centerIn: parent
        width: parent.width - XsStyleSheet.panelPadding * 2
        visible: notesState === "error"
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        text: notesError
        color: XsStyleSheet.secondaryTextColor
        font.pixelSize: XsStyleSheet.fontSize
        font.family: XsStyleSheet.fontFamily
    }

    Text {
        anchors.centerIn: parent
        visible: notesState === "ready" && root.noteRows.length === 0
        text: "No notes"
        color: XsStyleSheet.secondaryTextColor
        font.pixelSize: XsStyleSheet.fontSize
        font.family: XsStyleSheet.fontFamily
    }

    Flickable {
        id: notesFlick
        anchors.fill: parent
        visible: notesState === "ready" && root.noteRows.length > 0
        contentWidth: width
        contentHeight: notesColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 100000
        maximumFlickVelocity: 800

        // Leave room for the vertical scrollbar so notes are not clipped.
        property int rightSpacing: height < contentHeight ? 12 : 0

        Column {
            id: notesColumn
            width: notesFlick.width - notesFlick.rightSpacing
            height: implicitHeight
            spacing: 0

            Text {
                x: XsStyleSheet.panelPadding
                width: parent.width - XsStyleSheet.panelPadding * 2
                visible: root.noteStatusError !== ""
                wrapMode: Text.WordWrap
                text: root.noteStatusError
                color: XsStyleSheet.secondaryTextColor
                font.pixelSize: XsStyleSheet.fontSize
                font.family: XsStyleSheet.fontFamily
            }

            Repeater {
                model: root.noteRows

                delegate: Column {
                    x: XsStyleSheet.panelPadding
                    width: notesColumn.width - XsStyleSheet.panelPadding * 2
                    spacing: 4

                    required property var modelData
                    required property int index

                    RowLayout {
                        width: parent.width
                        spacing: 8

                        DetailLinkText {
                            Layout.fillWidth: true
                            text: root.textOrEmpty(modelData.name)
                            entityType: "Note"
                            entityId: modelData.id
                            font.bold: true
                            wrapMode: Text.WordWrap
                            onEntityClicked: function(type, id) { root.openNote(id) }
                        }

                        StatusBadge {
                            Layout.alignment: Qt.AlignTop
                            visible: !root.noteStatusEditable && root.textOrEmpty(modelData.status) !== ""
                            code: root.textOrEmpty(modelData.status)
                            label: root.labelFor(modelData.status)
                            tone: root.colorFor(modelData.status)
                        }

                        StatusMenu {
                            Layout.alignment: Qt.AlignTop
                            visible: root.noteStatusEditable
                            code: root.textOrEmpty(modelData.status)
                            label: root.labelFor(modelData.status)
                            tone: root.colorFor(modelData.status)
                            editable: true
                            busy: root.noteStatusBusy
                            codes: root.noteCodes()
                            labelOf: function(key) { return root.labelFor(key) }
                            toneOf: function(key) { return root.colorFor(key) }
                            onChosen: function(next) {
                                if (root.onNoteStatusEdit)
                                    root.onNoteStatusEdit(modelData.id, "status", next)
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        visible: root.textOrEmpty(modelData.author) !== "" || root.textOrEmpty(modelData.createdAt) !== ""
                        text: {
                            const parts = []
                            if (root.textOrEmpty(modelData.author) !== "")
                                parts.push(root.textOrEmpty(modelData.author))
                            if (root.textOrEmpty(modelData.createdAt) !== "")
                                parts.push(root.whenLabel(modelData.createdAt))
                            return parts.join(" · ")
                        }
                        color: XsStyleSheet.hintColor
                        font.pixelSize: XsStyleSheet.fontSize
                        font.family: XsStyleSheet.fontFamily
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        visible: root.people(modelData.to) !== ""
                        text: "To  " + root.people(modelData.to)
                        wrapMode: Text.WordWrap
                        color: XsStyleSheet.secondaryTextColor
                        font.pixelSize: XsStyleSheet.fontSize
                        font.family: XsStyleSheet.fontFamily
                    }

                    Text {
                        width: parent.width
                        visible: root.textOrEmpty(modelData.department) !== ""
                        text: "Department  " + root.textOrEmpty(modelData.department)
                        color: XsStyleSheet.secondaryTextColor
                        font.pixelSize: XsStyleSheet.fontSize
                        font.family: XsStyleSheet.fontFamily
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        visible: root.textOrEmpty(modelData.content) !== ""
                        text: root.textOrEmpty(modelData.content)
                        wrapMode: Text.WordWrap
                        color: XsStyleSheet.primaryTextColor
                        font.pixelSize: XsStyleSheet.fontSize
                        font.family: XsStyleSheet.fontFamily
                    }

                    Flow {
                        width: parent.width
                        spacing: 6
                        visible: root.asList(modelData.attachments).length > 0

                        Repeater {
                            model: root.asList(modelData.attachments)

                            delegate: Rectangle {
                                required property var modelData
                                width: 120
                                height: 68
                                color: "black"
                                border.width: 1
                                border.color: XsStyleSheet.widgetBgNormalColor
                                visible: root.imageSource(modelData.image) !== ""

                                Image {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    source: root.imageSource(modelData.image)
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.openAttachment(
                                        root.imageSource(modelData.image),
                                        root.textOrEmpty(modelData.name))
                                }
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: XsStyleSheet.widgetBgNormalColor
                        visible: index < root.noteRows.length - 1
                    }
                }
            }
        }

        // One row per wheel notch, and the flick stops at once.
        WheelHandler {
            onWheel: function(event) {
                const row = XsStyleSheet.widgetStdHeight
                const step = event.angleDelta.y !== 0
                             ? (event.angleDelta.y / 120) * row
                             : Math.max(-row, Math.min(row, event.pixelDelta.y))
                const limit = Math.max(0, notesFlick.contentHeight - notesFlick.height)
                notesFlick.contentY = Math.max(0, Math.min(notesFlick.contentY - step, limit))
                event.accepted = true
            }
        }

        ScrollBar.vertical: XsScrollBar {
            visible: notesFlick.height < notesFlick.contentHeight
            policy: ScrollBar.AsNeeded
        }
    }

    Popup {
        id: previewPopup
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        parent: Overlay.overlay
        width: 640
        height: 420
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: XsStyleSheet.panelPadding

        background: Rectangle {
            color: XsStyleSheet.panelBgColor
            border.width: 1
            border.color: XsStyleSheet.widgetBgNormalColor
        }

        contentItem: ColumnLayout {
            spacing: 6

            Text {
                Layout.fillWidth: true
                text: root.previewName
                elide: Text.ElideRight
                color: XsStyleSheet.primaryTextColor
                font.pixelSize: XsStyleSheet.fontSize
                font.family: XsStyleSheet.fontFamily
                font.bold: true
            }

            Image {
                Layout.fillWidth: true
                Layout.fillHeight: true
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                source: root.previewSource
            }
        }
    }
}
