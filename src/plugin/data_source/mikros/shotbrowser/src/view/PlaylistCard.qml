// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts

import xStudio 1.0

// One playlist header: name, notes, departments, date, category, and author.
Rectangle {
    id: card

    // Playlist tree injects one JSON object per header row.
    required property var modelData

    property bool expanded: false
    property bool expandable: false

    signal toggleRequested()
    signal openRequested()
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

    function shown(value) {
        return value === "" ? "N/A" : value
    }

    function datePart(iso) {
        if (!iso)
            return ""
        const cut = iso.indexOf("T")
        return cut >= 0 ? iso.substring(0, cut) : iso
    }

    function timePart(iso) {
        if (!iso)
            return ""
        const cut = iso.indexOf("T")
        if (cut < 0)
            return ""
        const clock = iso.substring(cut + 1)
        return clock.length >= 5 ? clock.substring(0, 5) : clock
    }

    readonly property string label: field("name")
    readonly property int versionCount: {
        const row = rowBag()
        if (!row || row.versionCount === undefined || row.versionCount === null)
            return 0
        return Number(row.versionCount)
    }
    readonly property int openNotesCount: {
        const row = rowBag()
        if (!row || row.openNotesCount === undefined || row.openNotesCount === null)
            return 0
        return Number(row.openNotesCount)
    }
    readonly property bool hasOpenNotes: openNotesCount > 0
    readonly property string departments: field("departments")
    readonly property string createdAt: field("createdAt")
    readonly property string category: field("category")
    readonly property string author: field("author")

    width: parent ? parent.width : 0
    height: XsStyleSheet.widgetStdHeight * 2
    color: XsStyleSheet.widgetBgNormalColor
    border.width: 1
    border.color: rowHover.hovered ? XsStyleSheet.accentColor : "transparent"

    MouseArea {
        anchors.fill: parent
        z: -1
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onDoubleClicked: card.openRequested()
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton)
                card.menuRequested(mouse.x, mouse.y)
        }
    }

    GridLayout {
        anchors.fill: parent
        anchors.margins: XsStyleSheet.panelPadding
        rows: 2
        columns: 7
        rowSpacing: 1
        columnSpacing: 3

        XsPrimaryButton {
            Layout.preferredWidth: 20
            Layout.fillHeight: true
            Layout.rowSpan: 2
            imgSrc: "qrc:/icons/chevron_right.svg"
            imageDiv.rotation: card.expanded ? 90 : 0
            enabled: card.expandable
            opacity: enabled ? 1 : 0.5
            isActiveViaIndicator: false
            isActive: card.expanded
            onClicked: card.toggleRequested()
        }

        XsPrimaryButton {
            Layout.preferredWidth: 20
            Layout.fillHeight: true
            Layout.rowSpan: 2
            text: "N"
            font.weight: card.hasOpenNotes ? Font.Bold : Font.Normal
            font.pixelSize: XsStyleSheet.fontSize * 1.2
            textDiv.color: card.hasOpenNotes
                           ? XsStyleSheet.accentColor
                           : XsStyleSheet.hintColor
            enabled: true
            bgDiv.opacity: card.hasOpenNotes ? 1 : 0.5
            isUnClickable: true
            isActiveViaIndicator: card.hasOpenNotes
        }

        XsText {
            Layout.preferredWidth: XsStyleSheet.widgetStdHeight
            Layout.fillHeight: true
            Layout.rowSpan: 2
            text: card.versionCount
            color: card.versionCount === 0
                   ? XsStyleSheet.hintColor
                   : XsStyleSheet.secondaryTextColor
            font.pixelSize: XsStyleSheet.fontSize * 1.2
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        XsText {
            Layout.columnSpan: 3
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: card.label
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignTop
            topPadding: 2
        }

        XsText {
            Layout.fillWidth: true
            Layout.minimumWidth: 20
            Layout.preferredWidth: 60
            Layout.maximumWidth: 190
            Layout.fillHeight: true
            text: card.shown(card.departments)
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignTop
            topPadding: 2
            rightPadding: 2
        }

        XsText {
            Layout.fillWidth: true
            Layout.minimumWidth: 40
            Layout.preferredWidth: 60
            Layout.maximumWidth: 92
            text: card.shown(card.datePart(card.createdAt))
            elide: Text.ElideRight
            color: XsStyleSheet.hintColor
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignBottom
            bottomPadding: 2
        }

        XsText {
            Layout.fillWidth: true
            Layout.minimumWidth: 40
            Layout.preferredWidth: 60
            Layout.maximumWidth: 85
            text: card.shown(card.timePart(card.createdAt))
            elide: Text.ElideRight
            color: XsStyleSheet.hintColor
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignBottom
            bottomPadding: 2
        }

        XsText {
            Layout.fillWidth: true
            Layout.minimumWidth: 60
            text: card.shown(card.category)
            elide: Text.ElideRight
            color: XsStyleSheet.hintColor
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignBottom
            bottomPadding: 2
        }

        XsText {
            Layout.fillWidth: true
            text: card.shown(card.author)
            elide: Text.ElideRight
            color: XsStyleSheet.hintColor
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignBottom
            bottomPadding: 2
            rightPadding: 2
        }
    }

    HoverHandler {
        id: rowHover
    }
}
