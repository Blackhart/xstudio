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
            const node = nodes[i]
            const name = String(node.name)
            const versionCount = node.versionCount !== undefined
                    ? Number(node.versionCount) : 0
            const entries = node.versions && node.versions.length ? node.versions : []
            const expanded = !!openPlaylists[name]
            rows.push({
                name: name,
                id: node.id !== undefined ? node.id : 0,
                versionCount: versionCount,
                openNotesCount: node.openNotesCount !== undefined
                        ? Number(node.openNotesCount) : 0,
                departments: field(node, "departments"),
                createdAt: field(node, "createdAt"),
                category: field(node, "category"),
                author: field(node, "author"),
                depth: 0,
                parent: versionCount > 0,
                open: expanded,
                kind: "playlist",
                childCount: versionCount
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

    XsAttributeValue {
        id: loadMorePlaylists
        attributeTitle: "LoadMorePlaylists"
        model: attributes
    }

    CatalogValue { id: footer; title: "PlaylistTreeFooter"; source: attributes }

    Connections {
        target: selectedProject
        function onValueChanged() {
            playlists.openPlaylists = ({})
        }
    }

    property int loadMoreToken: 0
    property var treeRows: []

    // Watch the attribute without putting contentY in the `names:` binding (binding loop).
    property var treeSource: playlistTreeAttr.value
    onTreeSourceChanged: {
        // clearPlaylists publishes []; reset chevrons (openPlaylists is not ExpandedPlaylist).
        if (playlistTreeNodes().length === 0)
            openPlaylists = ({})
        reloadTreeRows()
    }
    onOpenPlaylistsChanged: reloadTreeRows()

    // Hold scroll on append / thumbs; reset to top on clear or first page.
    function reloadTreeRows() {
        const rows = playlistTreeRows()
        const previousCount = Array.isArray(treeRows) ? treeRows.length : 0
        const nextCount = rows.length
        if (nextCount === 0 || previousCount === 0)
            playlistColumn.resetScrollToTop()
        else
            playlistColumn.holdScrollAt(playlistColumn.contentYForHold())
        treeRows = rows
    }

    NameColumn {
        id: playlistColumn
        anchors.fill: parent
        resultSwitcher: true
        switcherHeight: playlists.switcherHeight
        switcherSpacing: playlists.switcherSpacing
        resultMode: playlists.resultMode
        onResultChosen: function(mode) { playlists.resultChosen(mode) }
        loadedFooter: true
        customFooter: true
        footerText: footer.value
        listColor: XsStyleSheet.panelBgColor
        tree: true
        playlistTree: true
        names: playlists.treeRows
        infiniteScroll: true
        onLoadMoreRequested: {
            playlists.loadMoreToken += 1
            loadMorePlaylists.value = String(playlists.loadMoreToken)
        }
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

            width: ListView.view.width - (ListView.view.rightSpacing || 0)
            height: versionLine
                    ? (XsStyleSheet.widgetStdHeight + 1) * 4
                    : XsStyleSheet.widgetStdHeight * 2

            PlaylistCard {
                visible: !row.versionLine
                width: row.width
                modelData: row.modelData
                expandable: row.isParent
                expanded: row.isOpen
                onToggleRequested: playlists.togglePlaylist(row.label)
            }

            VersionCard {
                id: versionCard
                visible: row.versionLine
                modelData: row.modelData
                indent: XsStyleSheet.widgetStdHeight / 1.2
                chosen: false
                onOpenCurrent: playlists.openRequested(versionCard.payload())
                onMenuRequested: function(x, y) {
                    playlists.menuRequested(versionCard.payload(), versionCard, x, y)
                }
            }
        }
    }
}
