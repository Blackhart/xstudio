// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import xstudio.qml.clipboard 1.0

import xStudio 1.0

// Sticky identity header + scroll body (Context / Media / Paths).
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

    function shotgridUrl(entityType, entityId) {
        if (!shotgridBaseUrl || !entityType || !entityId)
            return ""
        let base = String(shotgridBaseUrl)
        if (base.endsWith("/"))
            base = base.slice(0, -1)
        return base + "/detail/" + entityType + "/" + String(entityId)
    }

    function openUrl(url) {
        if (!url)
            return
        Qt.openUrlExternally(url)
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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Empty / not_mikros / error — centered, minimal chrome.
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
                    visible: {
                        if (detailState === "not_mikros")
                            return root.notMikrosMeta() !== ""
                        return root.centerSubtitle() !== ""
                    }
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

        // Sticky identity header (loading + ready).
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

                // Thumb 16:9 — skeleton while loading.
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

                    // Loading skeleton lines.
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

                    // Ready identity.
                    ColumnLayout {
                        visible: root.isReady
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            Layout.fillWidth: true
                            text: shown(version.name)
                            color: XsStyleSheet.accentColor
                            font.pixelSize: XsStyleSheet.fontSize * 1.1
                            font.family: XsStyleSheet.fontFamily
                            font.bold: true
                            font.underline: hasId(version) && root.shotgridUrl("Version", version.id) !== ""
                            wrapMode: Text.WordWrap
                            elide: Text.ElideRight

                            MouseArea {
                                anchors.fill: parent
                                enabled: hasId(version)
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: root.openUrl(root.shotgridUrl("Version", version.id))
                            }
                        }

                        // Breadcrumb: Project / Sequence / Shot|Asset
                        Flow {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                visible: hasId(project)
                                text: shown(project.name)
                                color: XsStyleSheet.accentColor
                                font.pixelSize: XsStyleSheet.fontSize
                                font.family: XsStyleSheet.fontFamily
                                font.underline: true

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.openUrl(root.shotgridUrl("Project", project.id))
                                }
                            }
                            Text {
                                visible: hasId(project) && (hasId(sequence) || hasId(shot) || hasId(asset))
                                text: "/"
                                color: XsStyleSheet.secondaryTextColor
                                font.pixelSize: XsStyleSheet.fontSize
                                font.family: XsStyleSheet.fontFamily
                            }
                            Text {
                                visible: hasId(sequence)
                                text: shown(sequence.name)
                                color: XsStyleSheet.accentColor
                                font.pixelSize: XsStyleSheet.fontSize
                                font.family: XsStyleSheet.fontFamily
                                font.underline: true

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.openUrl(root.shotgridUrl("Sequence", sequence.id))
                                }
                            }
                            Text {
                                visible: hasId(sequence) && (hasId(shot) || hasId(asset))
                                text: "/"
                                color: XsStyleSheet.secondaryTextColor
                                font.pixelSize: XsStyleSheet.fontSize
                                font.family: XsStyleSheet.fontFamily
                            }
                            Text {
                                visible: hasId(shot)
                                text: shown(shot.name)
                                color: XsStyleSheet.accentColor
                                font.pixelSize: XsStyleSheet.fontSize
                                font.family: XsStyleSheet.fontFamily
                                font.underline: true

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.openUrl(root.shotgridUrl("Shot", shot.id))
                                }
                            }
                            Text {
                                visible: !hasId(shot) && hasId(asset)
                                text: shown(asset.name)
                                color: XsStyleSheet.accentColor
                                font.pixelSize: XsStyleSheet.fontSize
                                font.family: XsStyleSheet.fontFamily
                                font.underline: true

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.openUrl(root.shotgridUrl("Asset", asset.id))
                                }
                            }
                        }

                        // Badges: tech + artistic + step + notes
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6

                            StatusBadge {
                                code: hasText(status.technical) ? String(status.technical) : ""
                                visible: hasText(status.technical)
                            }
                            StatusBadge {
                                code: hasText(status.artistic) ? String(status.artistic) : ""
                                visible: hasText(status.artistic)
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

        // Scroll body — Context / Media / Paths.
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

                // Context
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: XsStyleSheet.panelPadding
                    Layout.rightMargin: XsStyleSheet.panelPadding
                    Layout.topMargin: XsStyleSheet.panelPadding
                    spacing: 6

                    Text {
                        text: "Context"
                        color: XsStyleSheet.primaryTextColor
                        font.pixelSize: XsStyleSheet.fontSize
                        font.family: XsStyleSheet.fontFamily
                        font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: hasId(project)

                        Text {
                            Layout.preferredWidth: 88
                            text: "Project"
                            color: XsStyleSheet.secondaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                        Text {
                            text: shown(project.name)
                            color: XsStyleSheet.accentColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                            font.underline: true

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.openUrl(root.shotgridUrl("Project", project.id))
                            }
                        }
                        Text {
                            visible: hasText(project.type) || hasText(project.code)
                            Layout.fillWidth: true
                            text: "· " + (hasText(project.type) ? String(project.type) : String(project.code))
                            color: XsStyleSheet.primaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                            elide: Text.ElideRight
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: hasId(sequence)

                        Text {
                            Layout.preferredWidth: 88
                            text: "Sequence"
                            color: XsStyleSheet.secondaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                        Text {
                            Layout.fillWidth: true
                            text: shown(sequence.name)
                            color: XsStyleSheet.accentColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                            font.underline: true

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.openUrl(root.shotgridUrl("Sequence", sequence.id))
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: hasId(shot)

                        Text {
                            Layout.preferredWidth: 88
                            text: "Shot"
                            color: XsStyleSheet.secondaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                        Text {
                            Layout.fillWidth: true
                            text: shown(shot.name)
                            color: XsStyleSheet.accentColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                            font.underline: true

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.openUrl(root.shotgridUrl("Shot", shot.id))
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: !hasId(shot) && hasId(asset)

                        Text {
                            Layout.preferredWidth: 88
                            text: "Asset"
                            color: XsStyleSheet.secondaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                        Text {
                            Layout.fillWidth: true
                            text: shown(asset.name)
                            color: XsStyleSheet.accentColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                            font.underline: true

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.openUrl(root.shotgridUrl("Asset", asset.id))
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    Layout.topMargin: XsStyleSheet.panelPadding
                    color: XsStyleSheet.widgetBgNormalColor
                }

                // Media
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

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.preferredWidth: 88
                            text: "Type"
                            color: XsStyleSheet.secondaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                        Text {
                            Layout.fillWidth: true
                            text: shown(media.kind)
                            color: XsStyleSheet.primaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: hasText(media.path)

                        Text {
                            Layout.preferredWidth: 88
                            text: "Path"
                            color: XsStyleSheet.secondaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                        Text {
                            Layout.fillWidth: true
                            text: textOrEmpty(media.path)
                            color: XsStyleSheet.primaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                            elide: Text.ElideMiddle

                            ToolTip.visible: mediaPathHover.hovered
                            ToolTip.delay: 400
                            ToolTip.text: textOrEmpty(media.path)

                            HoverHandler {
                                id: mediaPathHover
                            }
                        }
                        XsPrimaryButton {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: XsStyleSheet.widgetStdHeight - 4
                            text: "Copy"
                            onClicked: root.copyText(media.path)
                        }
                    }

                    // Sequence extras
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: media.kind === "sequence" && (hasText(media.frameRange) || media.frameCount)

                        Text {
                            Layout.preferredWidth: 88
                            text: "Frames"
                            color: XsStyleSheet.secondaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                        Text {
                            Layout.fillWidth: true
                            text: {
                                const parts = []
                                if (hasText(media.frameRange))
                                    parts.push(String(media.frameRange))
                                if (media.frameCount)
                                    parts.push(String(media.frameCount))
                                return parts.join(" · ")
                            }
                            color: XsStyleSheet.primaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                    }

                    // Video extras
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: media.kind === "video" && hasText(media.container)

                        Text {
                            Layout.preferredWidth: 88
                            text: "Container"
                            color: XsStyleSheet.secondaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                        Text {
                            Layout.fillWidth: true
                            text: shown(media.container)
                            color: XsStyleSheet.primaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: media.kind === "video" && media.fps

                        Text {
                            Layout.preferredWidth: 88
                            text: "FPS"
                            color: XsStyleSheet.secondaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Number(media.fps).toFixed(3)
                            color: XsStyleSheet.primaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: media.kind === "video" && (media.frameCount || media.duration)

                        Text {
                            Layout.preferredWidth: 88
                            text: "Duration"
                            color: XsStyleSheet.secondaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                        Text {
                            Layout.fillWidth: true
                            text: {
                                const parts = []
                                if (media.frameCount)
                                    parts.push(String(media.frameCount) + " frames")
                                if (media.duration)
                                    parts.push(Number(media.duration).toFixed(3) + " s")
                                return parts.join(" · ")
                            }
                            color: XsStyleSheet.primaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    Layout.topMargin: XsStyleSheet.panelPadding
                    color: XsStyleSheet.widgetBgNormalColor
                }

                // Paths — only non-empty rows; no thumbnail text line.
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

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: hasText(files.frames)

                        Text {
                            Layout.preferredWidth: 88
                            text: "Frames"
                            color: XsStyleSheet.secondaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                        Text {
                            Layout.fillWidth: true
                            text: textOrEmpty(files.frames)
                            color: XsStyleSheet.primaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                            elide: Text.ElideMiddle

                            ToolTip.visible: framesPathHover.hovered
                            ToolTip.delay: 400
                            ToolTip.text: textOrEmpty(files.frames)

                            HoverHandler {
                                id: framesPathHover
                            }
                        }
                        XsPrimaryButton {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: XsStyleSheet.widgetStdHeight - 4
                            text: "Copy"
                            onClicked: root.copyText(files.frames)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: hasText(files.movie)

                        Text {
                            Layout.preferredWidth: 88
                            text: "Movie"
                            color: XsStyleSheet.secondaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                        Text {
                            Layout.fillWidth: true
                            text: textOrEmpty(files.movie)
                            color: XsStyleSheet.primaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                            elide: Text.ElideMiddle

                            ToolTip.visible: moviePathHover.hovered
                            ToolTip.delay: 400
                            ToolTip.text: textOrEmpty(files.movie)

                            HoverHandler {
                                id: moviePathHover
                            }
                        }
                        XsPrimaryButton {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: XsStyleSheet.widgetStdHeight - 4
                            text: "Copy"
                            onClicked: root.copyText(files.movie)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: hasText(files.uploadedMovie)

                        Text {
                            Layout.preferredWidth: 88
                            text: "Uploaded"
                            color: XsStyleSheet.secondaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                        Text {
                            Layout.fillWidth: true
                            text: textOrEmpty(files.uploadedMovie)
                            color: XsStyleSheet.primaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                            elide: Text.ElideMiddle

                            ToolTip.visible: uploadedPathHover.hovered
                            ToolTip.delay: 400
                            ToolTip.text: textOrEmpty(files.uploadedMovie)

                            HoverHandler {
                                id: uploadedPathHover
                            }
                        }
                        XsPrimaryButton {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: XsStyleSheet.widgetStdHeight - 4
                            text: "Copy"
                            onClicked: root.copyText(files.uploadedMovie)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: hasText(files.sites)

                        Text {
                            Layout.preferredWidth: 88
                            text: "Sites"
                            color: XsStyleSheet.secondaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                        Text {
                            Layout.fillWidth: true
                            text: textOrEmpty(files.sites)
                            color: XsStyleSheet.primaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                            elide: Text.ElideRight
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: files.syncedToClient === true

                        Text {
                            Layout.preferredWidth: 88
                            text: "Client"
                            color: XsStyleSheet.secondaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "Synced"
                            color: XsStyleSheet.primaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize
                            font.family: XsStyleSheet.fontFamily
                        }
                    }
                }

                Item {
                    Layout.preferredHeight: XsStyleSheet.panelPadding * 2
                }
            }
        }
    }
}
