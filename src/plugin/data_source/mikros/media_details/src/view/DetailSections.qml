// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import xstudio.qml.clipboard 1.0

import xStudio 1.0

// Sticky identity header + scroll body (Version / parents / Paths / Media).
Item {
    id: root

    property var detail: ({})
    property string shotgridBaseUrl: ""
    property string detailState: "empty"
    property string detailError: ""

    readonly property bool isReady: detailState === "ready"
    readonly property bool isLoading: detailState === "loading"
    readonly property bool showHeader: isReady || isLoading
    readonly property bool showCenter:
        detailState === "empty" || detailState === "not_mikros" || detailState === "error"

    readonly property var project: detail && detail.project ? detail.project : ({})
    readonly property var sequence: detail && detail.sequence ? detail.sequence : ({})
    readonly property var shot: detail && detail.shot ? detail.shot : ({})
    readonly property var asset: detail && detail.asset ? detail.asset : ({})
    readonly property var version: detail && detail.version ? detail.version : ({})
    readonly property var status: detail && detail.status ? detail.status : ({})
    readonly property var team: detail && detail.team ? detail.team : ({})
    readonly property var media: detail && detail.media ? detail.media : ({})
    readonly property var files: detail && detail.files ? detail.files : ({})
    readonly property var playlists: version && version.playlists ? version.playlists : []

    Clipboard {
        id: clipboard
    }

    function shown(value) {
        if (value === undefined || value === null || value === "")
            return "N/A"
        return String(value)
    }

    function textOrEmpty(value) {
        if (value === undefined || value === null)
            return ""
        return String(value)
    }

    function hasId(obj) {
        return obj && obj.id !== undefined && obj.id !== null && Number(obj.id) > 0
    }

    function hasText(value) {
        return textOrEmpty(value) !== ""
    }

    function hasNumber(value) {
        return value !== undefined && value !== null && Number(value) !== 0
    }

    function isTrue(value) {
        return value === true || value === 1 || value === "1"
    }

    function shotgridUrl(entityType, entityId) {
        if (!shotgridBaseUrl || !entityType || !entityId)
            return ""
        let base = String(shotgridBaseUrl)
        if (base.endsWith("/"))
            base = base.slice(0, -1)
        return base + "/Detail/" + entityType + "/" + String(entityId)
    }

    function openUrl(url) {
        if (!url)
            return
        Qt.openUrlExternally(url)
    }

    function openEntity(entityType, entityId) {
        openUrl(shotgridUrl(entityType, entityId))
    }

    function copyText(value) {
        const text = textOrEmpty(value)
        if (text === "")
            return
        clipboard.text = text
    }

    function thumbSource() {
        const url = textOrEmpty(files.image)
        if (url === "")
            return "qrc:/mikros_media_details_icons/no_preview.jpg"
        if (url.startsWith("file:") || url.startsWith("http:") || url.startsWith("https:") || url.startsWith("qrc:"))
            return url
        if (url.startsWith("/"))
            return "file://" + url
        return "qrc:/mikros_media_details_icons/no_preview.jpg"
    }

    function notMikrosMeta() {
        const kind = textOrEmpty(media.kind)
        if (kind === "video") {
            const parts = ["video"]
            if (media.fps)
                parts.push(Number(media.fps).toFixed(3) + " fps")
            if (media.frameCount)
                parts.push(String(media.frameCount) + " frames")
            return parts.join(" · ")
        }
        if (kind === "sequence") {
            const parts = ["sequence"]
            if (hasText(media.frameRange))
                parts.push(String(media.frameRange))
            if (media.frameCount)
                parts.push(String(media.frameCount) + " frames")
            return parts.join(" · ")
        }
        return ""
    }

    function centerTitle() {
        if (detailState === "error")
            return "Version could not be loaded"
        if (detailState === "not_mikros")
            return "Not a Mikros version"
        return "No media selected"
    }

    function centerSubtitle() {
        if (detailState === "error") {
            if (detailError !== "")
                return detailError
            if (hasId(version))
                return "id " + String(version.id)
            return ""
        }
        if (detailState === "empty")
            return "Select a clip in the viewport"
        return ""
    }

    function metaLine() {
        const parts = []
        if (hasText(version.author))
            parts.push(String(version.author))
        else if (hasText(team.artist))
            parts.push(String(team.artist))
        if (hasText(version.createdAt))
            parts.push(String(version.createdAt))
        if (hasText(version.frameRange))
            parts.push(String(version.frameRange))
        return parts.join(" · ")
    }

    function openNotesLabel() {
        const count = files.openNotesCount !== undefined ? Number(files.openNotesCount) : 0
        if (!count || count <= 0)
            return ""
        return count === 1 ? "1 note" : String(count) + " notes"
    }

    function reviewLine() {
        const parts = []
        if (isTrue(version.clientReviewed))
            parts.push("client reviewed")
        if (isTrue(version.clientApproved))
            parts.push("approved")
        return parts.join(" · ")
    }

    function framesLine() {
        const parts = []
        if (hasText(version.frameRange))
            parts.push(String(version.frameRange))
        if (hasNumber(version.frameCount))
            parts.push(String(version.frameCount))
        if (hasNumber(version.versionNumber))
            parts.push("v" + String(version.versionNumber))
        if (isTrue(version.isLatest))
            parts.push("latest")
        return parts.join(" · ")
    }

    function taskStepLine() {
        const parts = []
        if (hasText(version.task))
            parts.push(String(version.task))
        if (hasText(version.step))
            parts.push(String(version.step))
        return parts.join(" / ")
    }

    function statusLine() {
        const parts = []
        if (hasText(status.technical))
            parts.push("tech " + String(status.technical))
        if (hasText(status.artistic))
            parts.push("art " + String(status.artistic))
        return parts.join(" · ")
    }

    function sequenceShotsLine() {
        const parts = []
        if (hasNumber(sequence.shotCount) || hasNumber(sequence.estimatedShotsCount)) {
            const current = hasNumber(sequence.shotCount) ? String(sequence.shotCount) : "—"
            const estimated = hasNumber(sequence.estimatedShotsCount)
                             ? String(sequence.estimatedShotsCount) + " estimated"
                             : ""
            parts.push(estimated !== "" ? current + " / " + estimated : current)
        }
        if (hasNumber(sequence.estimatedFramesCount))
            parts.push(String(sequence.estimatedFramesCount) + " frames est.")
        return parts.join(" · ")
    }

    function shotCutLine() {
        const parts = []
        if (hasNumber(shot.cutIn))
            parts.push("in " + String(shot.cutIn))
        if (hasNumber(shot.cutOut))
            parts.push("out " + String(shot.cutOut))
        if (hasNumber(shot.cutOrder))
            parts.push("order " + String(shot.cutOrder))
        return parts.join(" · ")
    }

    function durationSec(value) {
        return hasNumber(value) ? String(value) + " s" : ""
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.showCenter

            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - XsStyleSheet.panelPadding * 2
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: XsStyleSheet.secondaryTextColor
                    font.pixelSize: XsStyleSheet.fontSize
                    font.family: XsStyleSheet.fontFamily
                    text: root.centerTitle()
                }
                Text {
                    visible: detailState === "not_mikros" && hasText(media.path)
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WrapAnywhere
                    color: XsStyleSheet.hintColor
                    font.pixelSize: XsStyleSheet.fontSize
                    font.family: XsStyleSheet.fontFamily
                    text: textOrEmpty(media.path)
                }
                Text {
                    visible: detailState === "not_mikros"
                             ? root.notMikrosMeta() !== ""
                             : root.centerSubtitle() !== ""
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: XsStyleSheet.hintColor
                    font.pixelSize: XsStyleSheet.fontSize
                    font.family: XsStyleSheet.fontFamily
                    text: detailState === "not_mikros" ? root.notMikrosMeta() : root.centerSubtitle()
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: headerRow.implicitHeight + XsStyleSheet.panelPadding * 2
            visible: root.showHeader

            RowLayout {
                id: headerRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: XsStyleSheet.panelPadding
                spacing: XsStyleSheet.panelPadding

                Rectangle {
                    Layout.preferredWidth: 140
                    Layout.preferredHeight: 79
                    Layout.alignment: Qt.AlignTop
                    color: "black"
                    border.width: 1
                    border.color: XsStyleSheet.widgetBgNormalColor

                    Rectangle {
                        anchors.fill: parent
                        visible: root.isLoading
                        color: XsStyleSheet.widgetBgNormalColor
                        opacity: 0.85
                    }
                    Image {
                        anchors.centerIn: parent
                        width: parent.width
                        height: Math.min(parent.height, width * 9 / 16)
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        visible: root.isReady
                        source: root.thumbSource()
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 4

                    ColumnLayout {
                        visible: root.isLoading
                        Layout.fillWidth: true
                        spacing: 6
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 10
                            color: XsStyleSheet.widgetBgNormalColor
                            radius: 2
                        }
                        Rectangle {
                            Layout.preferredWidth: 120
                            Layout.preferredHeight: 10
                            color: XsStyleSheet.widgetBgNormalColor
                            radius: 2
                        }
                        Text {
                            text: "Loading…"
                            color: XsStyleSheet.secondaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                    }

                    ColumnLayout {
                        visible: root.isReady
                        Layout.fillWidth: true
                        spacing: 4

                        DetailLinkText {
                            Layout.fillWidth: true
                            text: shown(version.name)
                            entityType: "Version"
                            entityId: version.id
                            font.pixelSize: XsStyleSheet.fontSize * 1.1
                            font.bold: true
                            wrapMode: Text.WordWrap
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 4
                            DetailLinkText {
                                visible: hasId(project)
                                text: shown(project.name)
                                entityType: "Project"
                                entityId: project.id
                                onEntityClicked: function(type, id) { root.openEntity(type, id) }
                            }
                            Text {
                                visible: hasId(project) && (hasId(sequence) || hasId(shot) || hasId(asset))
                                text: "/"
                                color: XsStyleSheet.secondaryTextColor
                                font.pixelSize: XsStyleSheet.fontSize
                                font.family: XsStyleSheet.fontFamily
                            }
                            DetailLinkText {
                                visible: hasId(sequence)
                                text: shown(sequence.name)
                                entityType: "Sequence"
                                entityId: sequence.id
                                onEntityClicked: function(type, id) { root.openEntity(type, id) }
                            }
                            Text {
                                visible: hasId(sequence) && (hasId(shot) || hasId(asset))
                                text: "/"
                                color: XsStyleSheet.secondaryTextColor
                                font.pixelSize: XsStyleSheet.fontSize
                                font.family: XsStyleSheet.fontFamily
                            }
                            DetailLinkText {
                                visible: hasId(shot)
                                text: shown(shot.name)
                                entityType: "Shot"
                                entityId: shot.id
                                onEntityClicked: function(type, id) { root.openEntity(type, id) }
                            }
                            DetailLinkText {
                                visible: !hasId(shot) && hasId(asset)
                                text: shown(asset.name)
                                entityType: "Asset"
                                entityId: asset.id
                                onEntityClicked: function(type, id) { root.openEntity(type, id) }
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            StatusBadge {
                                visible: hasText(status.technical)
                                code: hasText(status.technical) ? String(status.technical) : ""
                            }
                            StatusBadge {
                                visible: hasText(status.artistic)
                                code: hasText(status.artistic) ? String(status.artistic) : ""
                            }
                            Rectangle {
                                visible: hasText(version.step)
                                implicitHeight: XsStyleSheet.widgetStdHeight - 4
                                implicitWidth: stepChipText.implicitWidth + 12
                                color: "transparent"
                                border.width: 1
                                border.color: XsStyleSheet.widgetBgNormalColor
                                radius: 2
                                Text {
                                    id: stepChipText
                                    anchors.centerIn: parent
                                    text: textOrEmpty(version.step)
                                    color: XsStyleSheet.secondaryTextColor
                                    font.pixelSize: XsStyleSheet.fontSize
                                    font.family: XsStyleSheet.fontFamily
                                }
                            }
                            Rectangle {
                                visible: root.openNotesLabel() !== ""
                                implicitHeight: XsStyleSheet.widgetStdHeight - 4
                                implicitWidth: notesChipText.implicitWidth + 12
                                color: "transparent"
                                border.width: 1
                                border.color: XsStyleSheet.widgetBgNormalColor
                                radius: 2
                                Text {
                                    id: notesChipText
                                    anchors.centerIn: parent
                                    text: root.openNotesLabel()
                                    color: XsStyleSheet.secondaryTextColor
                                    font.pixelSize: XsStyleSheet.fontSize
                                    font.family: XsStyleSheet.fontFamily
                                }
                            }
                            Rectangle {
                                visible: isTrue(version.isLatest)
                                implicitHeight: XsStyleSheet.widgetStdHeight - 4
                                implicitWidth: latestChipText.implicitWidth + 12
                                color: "transparent"
                                border.width: 1
                                border.color: XsStyleSheet.widgetBgNormalColor
                                radius: 2
                                Text {
                                    id: latestChipText
                                    anchors.centerIn: parent
                                    text: "latest"
                                    color: XsStyleSheet.secondaryTextColor
                                    font.pixelSize: XsStyleSheet.fontSize
                                    font.family: XsStyleSheet.fontFamily
                                }
                            }
                            Rectangle {
                                visible: isTrue(files.syncedToClient)
                                implicitHeight: XsStyleSheet.widgetStdHeight - 4
                                implicitWidth: syncChipText.implicitWidth + 12
                                color: "transparent"
                                border.width: 1
                                border.color: XsStyleSheet.widgetBgNormalColor
                                radius: 2
                                Text {
                                    id: syncChipText
                                    anchors.centerIn: parent
                                    text: "synced"
                                    color: XsStyleSheet.secondaryTextColor
                                    font.pixelSize: XsStyleSheet.fontSize
                                    font.family: XsStyleSheet.fontFamily
                                }
                            }
                        }

                        Text {
                            visible: root.metaLine() !== ""
                            Layout.fillWidth: true
                            text: root.metaLine()
                            color: XsStyleSheet.hintColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.isReady
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: root.width
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: XsStyleSheet.widgetBgNormalColor
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: XsStyleSheet.panelPadding
                    Layout.rightMargin: XsStyleSheet.panelPadding
                    Layout.topMargin: XsStyleSheet.panelPadding
                    spacing: 6

                    DetailLinkText {
                        text: "Version"
                        entityType: "Version"
                        entityId: version.id
                        font.bold: true
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                    }
                    DetailFieldRow {
                        label: "Client name"
                        value: textOrEmpty(version.clientCode)
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                    DetailFieldRow {
                        label: "Description"
                        value: textOrEmpty(version.description)
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                    DetailFieldRow {
                        label: "Artist"
                        value: textOrEmpty(version.author)
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                    DetailFieldRow {
                        label: "Task / Step"
                        value: root.taskStepLine()
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                    DetailFieldRow {
                        label: "Status"
                        value: root.statusLine()
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                    DetailFieldRow {
                        label: "Review"
                        value: root.reviewLine()
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                    DetailFieldRow {
                        label: "Frames"
                        value: root.framesLine()
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                    DetailFieldRow {
                        label: "Scene"
                        value: textOrEmpty(version.workSourceScene)
                        copyable: true
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: playlists && playlists.length > 0
                        Text {
                            Layout.preferredWidth: 88
                            Layout.alignment: Qt.AlignTop
                            text: "Playlists"
                            color: XsStyleSheet.secondaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: root.playlists
                                delegate: Rectangle {
                                    required property var modelData
                                    implicitHeight: XsStyleSheet.widgetStdHeight - 4
                                    implicitWidth: playlistChipText.implicitWidth + 12
                                    color: "transparent"
                                    border.width: 1
                                    border.color: XsStyleSheet.widgetBgNormalColor
                                    radius: 2
                                    Text {
                                        id: playlistChipText
                                        anchors.centerIn: parent
                                        text: modelData && modelData.name ? String(modelData.name) : ""
                                        color: XsStyleSheet.accentColor
                                        font.pixelSize: XsStyleSheet.fontSize
                                        font.family: XsStyleSheet.fontFamily
                                        font.underline: true
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (modelData && modelData.id)
                                                root.openEntity("Playlist", modelData.id)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    visible: hasId(shot)
                    Layout.fillWidth: true
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        Layout.topMargin: XsStyleSheet.panelPadding
                        color: XsStyleSheet.widgetBgNormalColor
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: XsStyleSheet.panelPadding
                        Layout.rightMargin: XsStyleSheet.panelPadding
                        Layout.topMargin: XsStyleSheet.panelPadding
                        spacing: 6
                        DetailLinkText {
                            text: shown(shot.name)
                            entityType: "Shot"
                            entityId: shot.id
                            font.bold: true
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        }
                        DetailFieldRow {
                            label: "Status"
                            value: textOrEmpty(shot.status)
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                            onCopyRequested: function(text) { root.copyText(text) }
                        }
                        DetailFieldRow {
                            label: "Description"
                            value: textOrEmpty(shot.description)
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                            onCopyRequested: function(text) { root.copyText(text) }
                        }
                        DetailFieldRow {
                            label: "Cut"
                            value: root.shotCutLine()
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                            onCopyRequested: function(text) { root.copyText(text) }
                        }
                        DetailFieldRow {
                            label: "Duration"
                            value: root.durationSec(shot.cutDurationInSec)
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                            onCopyRequested: function(text) { root.copyText(text) }
                        }
                    }
                }

                ColumnLayout {
                    visible: !hasId(shot) && hasId(asset)
                    Layout.fillWidth: true
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        Layout.topMargin: XsStyleSheet.panelPadding
                        color: XsStyleSheet.widgetBgNormalColor
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: XsStyleSheet.panelPadding
                        Layout.rightMargin: XsStyleSheet.panelPadding
                        Layout.topMargin: XsStyleSheet.panelPadding
                        spacing: 6
                        DetailLinkText {
                            text: shown(asset.name)
                            entityType: "Asset"
                            entityId: asset.id
                            font.bold: true
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        }
                        DetailFieldRow {
                            label: "Status"
                            value: textOrEmpty(asset.status)
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                            onCopyRequested: function(text) { root.copyText(text) }
                        }
                        DetailFieldRow {
                            label: "Description"
                            value: textOrEmpty(asset.description)
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                            onCopyRequested: function(text) { root.copyText(text) }
                        }
                    }
                }

                ColumnLayout {
                    visible: hasId(sequence)
                    Layout.fillWidth: true
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        Layout.topMargin: XsStyleSheet.panelPadding
                        color: XsStyleSheet.widgetBgNormalColor
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: XsStyleSheet.panelPadding
                        Layout.rightMargin: XsStyleSheet.panelPadding
                        Layout.topMargin: XsStyleSheet.panelPadding
                        spacing: 6
                        DetailLinkText {
                            text: shown(sequence.name)
                            entityType: "Sequence"
                            entityId: sequence.id
                            font.bold: true
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        }
                        DetailFieldRow {
                            label: "Status"
                            value: textOrEmpty(sequence.status)
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                            onCopyRequested: function(text) { root.copyText(text) }
                        }
                        DetailFieldRow {
                            label: "Description"
                            value: textOrEmpty(sequence.description)
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                            onCopyRequested: function(text) { root.copyText(text) }
                        }
                        DetailFieldRow {
                            label: "Order"
                            value: hasNumber(sequence.sequenceOrder) ? String(sequence.sequenceOrder) : ""
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                            onCopyRequested: function(text) { root.copyText(text) }
                        }
                        DetailFieldRow {
                            label: "Duration"
                            value: root.durationSec(sequence.cutDurationInSec)
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                            onCopyRequested: function(text) { root.copyText(text) }
                        }
                        DetailFieldRow {
                            label: "Shots"
                            value: root.sequenceShotsLine()
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                            onCopyRequested: function(text) { root.copyText(text) }
                        }
                    }
                }

                ColumnLayout {
                    visible: hasId(project)
                    Layout.fillWidth: true
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        Layout.topMargin: XsStyleSheet.panelPadding
                        color: XsStyleSheet.widgetBgNormalColor
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: XsStyleSheet.panelPadding
                        Layout.rightMargin: XsStyleSheet.panelPadding
                        Layout.topMargin: XsStyleSheet.panelPadding
                        spacing: 6
                        DetailLinkText {
                            text: shown(project.name)
                            entityType: "Project"
                            entityId: project.id
                            font.bold: true
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        }
                        DetailFieldRow {
                            label: "Code"
                            value: textOrEmpty(project.code)
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                            onCopyRequested: function(text) { root.copyText(text) }
                        }
                        DetailFieldRow {
                            label: "Status"
                            value: textOrEmpty(project.status)
                            onEntityClicked: function(type, id) { root.openEntity(type, id) }
                            onCopyRequested: function(text) { root.copyText(text) }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    Layout.topMargin: XsStyleSheet.panelPadding
                    color: XsStyleSheet.widgetBgNormalColor
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: XsStyleSheet.panelPadding
                    Layout.rightMargin: XsStyleSheet.panelPadding
                    Layout.topMargin: XsStyleSheet.panelPadding
                    spacing: 6
                    Text {
                        text: "Paths"
                        color: XsStyleSheet.primaryTextColor
                        font.pixelSize: XsStyleSheet.fontSize
                        font.family: XsStyleSheet.fontFamily
                        font.bold: true
                    }
                    DetailFieldRow {
                        label: "Frames"
                        value: textOrEmpty(files.frames)
                        copyable: true
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                    DetailFieldRow {
                        label: "Movie"
                        value: textOrEmpty(files.movie)
                        copyable: true
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                    DetailFieldRow {
                        label: "Uploaded"
                        value: textOrEmpty(files.uploadedMovie)
                        copyable: true
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                    DetailFieldRow {
                        label: "Sites"
                        value: textOrEmpty(files.sites)
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                    DetailFieldRow {
                        label: "Client"
                        value: isTrue(files.syncedToClient) ? "Synced" : ""
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    Layout.topMargin: XsStyleSheet.panelPadding
                    color: XsStyleSheet.widgetBgNormalColor
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: XsStyleSheet.panelPadding
                    Layout.rightMargin: XsStyleSheet.panelPadding
                    Layout.topMargin: XsStyleSheet.panelPadding
                    spacing: 6
                    Text {
                        text: "Media"
                        color: XsStyleSheet.primaryTextColor
                        font.pixelSize: XsStyleSheet.fontSize
                        font.family: XsStyleSheet.fontFamily
                        font.bold: true
                    }
                    DetailFieldRow {
                        label: "Type"
                        value: textOrEmpty(media.kind)
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                    DetailFieldRow {
                        label: "Path"
                        value: textOrEmpty(media.path)
                        copyable: true
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                    DetailFieldRow {
                        label: "Frames"
                        value: {
                            if (media.kind !== "sequence")
                                return ""
                            const parts = []
                            if (hasText(media.frameRange))
                                parts.push(String(media.frameRange))
                            if (media.frameCount)
                                parts.push(String(media.frameCount))
                            return parts.join(" · ")
                        }
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                    DetailFieldRow {
                        label: "Container"
                        value: media.kind === "video" ? textOrEmpty(media.container) : ""
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                    DetailFieldRow {
                        label: "FPS"
                        value: media.kind === "video" && media.fps ? Number(media.fps).toFixed(3) : ""
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                    DetailFieldRow {
                        label: "Duration"
                        value: {
                            if (media.kind !== "video")
                                return ""
                            const parts = []
                            if (media.frameCount)
                                parts.push(String(media.frameCount) + " frames")
                            if (media.duration)
                                parts.push(Number(media.duration).toFixed(3) + " s")
                            return parts.join(" · ")
                        }
                        onEntityClicked: function(type, id) { root.openEntity(type, id) }
                        onCopyRequested: function(text) { root.copyText(text) }
                    }
                }

                Item {
                    Layout.preferredHeight: XsStyleSheet.panelPadding * 2
                }
            }
        }
    }
}
