// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts

import xStudio 1.0
import xstudio.qml.models 1.0

// Playlist names and the rows revealed when one is unfolded.
Item {
    id: playlists

    property string resultMode: "playlist"
    property int switcherHeight: XsStyleSheet.widgetStdHeight + 4
    property int switcherSpacing: 2
    property var openPlaylists: ({})

    signal resultChosen(string mode)
    signal openRequested(var row)
    signal menuRequested(var row, var item, real x, real y)

    function field(entry, key) {
        if (!entry || entry[key] === undefined || entry[key] === null)
            return ""
        return String(entry[key])
    }

    function flag(entry, key) {
        if (!entry || entry[key] === undefined || entry[key] === null)
            return false
        return entry[key] === true || entry[key] === 1 || entry[key] === "1"
    }

    function listPlaceholder() {
        const projectId = String(selectedProject.value ?? "")
        if (!projectId || projectId === "-1")
            return "No project selected"
        if (!playlistsLoaded.value)
            return "Loading…"
        return "No results"
    }

    function playlistTreeNodes() {
        let value = playlistTreeAttr.value
        if (!value)
            return []
        if (typeof value === "string") {
            try {
                value = JSON.parse(value)
            } catch (error) {
                return []
            }
        }
        return value.length ? value : []
    }

    function togglePlaylist(name) {
        const opening = !openPlaylists[name]
        const next = Object.assign({}, openPlaylists)
        next[name] = opening
        openPlaylists = next
        if (opening)
            expandedPlaylist.value = name
        else if (expandedPlaylist.value === name)
            expandedPlaylist.value = ""
    }

    function playlistTreeRows() {
        const nodes = playlistTreeNodes()
        const rows = []
        for (let i = 0; i < nodes.length; i++) {
            const name = String(nodes[i].name)
            const versionsLoaded = !!nodes[i].versionsLoaded
            const entries = nodes[i].versions && nodes[i].versions.length ? nodes[i].versions : []
            const expanded = !!openPlaylists[name]
            rows.push({
                name: name,
                depth: 0,
                parent: !versionsLoaded || entries.length > 0,
                open: expanded,
                kind: "playlist",
                childCount: entries.length
            })
            if (!expanded)
                continue
            for (let v = 0; v < entries.length; v++) {
                const entry = entries[v]
                const versionName = entry && entry.name !== undefined ? String(entry.name) : String(entry)
                const versionImage = entry && entry.image ? String(entry.image) : ""
                rows.push({
                    name: versionName,
                    image: versionImage,
                    author: field(entry, "author"),
                    createdAt: field(entry, "createdAt"),
                    frameRange: field(entry, "frameRange"),
                    description: field(entry, "description"),
                    step: field(entry, "step"),
                    status: field(entry, "status"),
                    artisticStatus: field(entry, "artisticStatus"),
                    sites: field(entry, "sites"),
                    hasNotes: flag(entry, "hasNotes"),
                    syncedToClient: flag(entry, "syncedToClient"),
                    id: entry && entry.id !== undefined ? entry.id : 0,
                    frames: field(entry, "frames"),
                    movie: field(entry, "movie"),
                    uploadedMovie: field(entry, "uploadedMovie"),
                    depth: 1,
                    parent: false,
                    open: false,
                    kind: "version",
                    childCount: 0
                })
            }
        }
        return rows
    }

    XsModuleData {
        id: attributes
        modelDataName: "mikros_shotbrowser_attributes"
    }

    XsAttributeValue {
        id: selectedProject
        attributeTitle: "SelectedProject"
        model: attributes
    }

    XsAttributeValue {
        id: playlistTreeAttr
        attributeTitle: "PlaylistTree"
        model: attributes
    }

    XsAttributeValue {
        id: playlistsLoaded
        attributeTitle: "PlaylistsLoaded"
        model: attributes
    }

    XsAttributeValue {
        id: expandedPlaylist
        attributeTitle: "ExpandedPlaylist"
        model: attributes
    }

    Connections {
        target: selectedProject
        function onValueChanged() {
            playlists.openPlaylists = ({})
        }
    }

    NameColumn {
        anchors.fill: parent
        resultSwitcher: true
        switcherHeight: playlists.switcherHeight
        switcherSpacing: playlists.switcherSpacing
        resultMode: playlists.resultMode
        onResultChosen: function(mode) { playlists.resultChosen(mode) }
        loadedFooter: true
        footerNoun: "playlist"
        listColor: XsStyleSheet.panelBgColor
        tree: true
        playlistTree: true
        names: playlists.playlistTreeRows()
        footerCount: playlists.playlistTreeNodes().length
        placeholder: {
            selectedProject.value
            playlistsLoaded.value
            return playlists.listPlaceholder()
        }
        rowDelegate: playlistRow
    }

    Component {
        id: playlistRow

        Item {
            id: row
            required property var modelData

            readonly property bool versionLine: modelData && modelData.kind === "version"
            readonly property string label: {
                if (modelData === undefined || modelData === null)
                    return ""
                if (modelData.name !== undefined)
                    return String(modelData.name)
                return String(modelData)
            }
            readonly property bool isParent: modelData && modelData.parent !== undefined ? modelData.parent : false
            readonly property bool isOpen: modelData && modelData.open !== undefined ? modelData.open : false
            readonly property int childCount: modelData && modelData.childCount !== undefined
                    ? modelData.childCount : 0

            width: ListView.view.width
            height: versionLine
                    ? (XsStyleSheet.widgetStdHeight + 1) * 4
                    : XsStyleSheet.widgetStdHeight * 2

            PlaylistCard {
                id: card
                visible: row.versionLine
                modelData: row.modelData
                onOpenCurrent: playlists.openRequested(card.payload())
                onMenuRequested: function(x, y) {
                    playlists.menuRequested(card.payload(), card, x, y)
                }
            }

            Rectangle {
                id: header
                visible: !row.versionLine
                width: row.width
                height: XsStyleSheet.widgetStdHeight * 2
                color: XsStyleSheet.widgetBgNormalColor
                border.width: 1
                border.color: rowHover.hovered ? XsStyleSheet.accentColor : "transparent"

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
                        imageDiv.rotation: row.isOpen ? 90 : 0
                        enabled: row.isParent
                        opacity: enabled ? 1 : 0.5
                        isActiveViaIndicator: false
                        isActive: row.isOpen
                        onClicked: playlists.togglePlaylist(row.label)
                    }

                    XsPrimaryButton {
                        Layout.preferredWidth: 20
                        Layout.fillHeight: true
                        Layout.rowSpan: 2
                        text: "N"
                        font.pixelSize: XsStyleSheet.fontSize * 1.2
                        textDiv.color: XsStyleSheet.hintColor
                        enabled: false
                        bgDiv.opacity: 0.5
                        isActiveViaIndicator: false
                        isUnClickable: true
                    }

                    XsText {
                        Layout.preferredWidth: XsStyleSheet.widgetStdHeight
                        Layout.fillHeight: true
                        Layout.rowSpan: 2
                        text: row.childCount
                        color: row.childCount === 0
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
                        text: row.label
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
                        text: "N/A"
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
                        text: "N/A"
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
                        text: "N/A"
                        elide: Text.ElideRight
                        color: XsStyleSheet.hintColor
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignBottom
                        bottomPadding: 2
                    }

                    XsText {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 60
                        text: "N/A"
                        elide: Text.ElideRight
                        color: XsStyleSheet.hintColor
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignBottom
                        bottomPadding: 2
                    }

                    XsText {
                        Layout.fillWidth: true
                        text: "N/A"
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
        }
    }
}
