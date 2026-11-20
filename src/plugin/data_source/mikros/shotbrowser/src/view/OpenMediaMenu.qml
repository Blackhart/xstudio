// SPDX-License-Identifier: Apache-2.0
import QtQuick

import xStudio 1.0
import xstudio.qml.helpers 1.0
import xstudio.qml.models 1.0

// Context menu for a version card. Writes OpenVersion / PreviewVersion for the view model.
Item {
    id: menu

    readonly property string previewPlaylistName: "preview"

    XsModuleData {
        id: attributes
        modelDataName: "mikros_shotbrowser_attributes"
    }

    XsAttributeValue {
        id: openVersionRequest
        attributeTitle: "OpenVersion"
        model: attributes
    }

    XsAttributeValue {
        id: previewVersionRequest
        attributeTitle: "PreviewVersion"
        model: attributes
    }

    function playlistNameOf(index) {
        if (!index || !index.valid)
            return ""
        let target = index
        if (theSessionData.getPlaylistIndex)
            target = theSessionData.getPlaylistIndex(index)
        if (!target || !target.valid)
            return ""
        if (theSessionData.get(target, "typeRole") !== "Playlist")
            return ""
        const name = theSessionData.get(target, "nameRole")
        if (name === undefined || name === null)
            return ""
        return String(name)
    }

    function currentPlaylistName() {
        const name = playlistNameOf(theSessionData.currentMediaContainerIndex)
        // Durable open must not target the ephemeral preview playlist.
        if (name === menu.previewPlaylistName)
            return ""
        return name
    }

    function sessionPlaylists() {
        const root = theSessionData.playlistsRootIdx
        const rows = []
        if (!root || !root.valid)
            return rows
        const count = theSessionData.rowCount(root)
        for (let i = 0; i < count; i++) {
            const idx = theSessionData.index(i, 0, root)
            const name = playlistNameOf(idx)
            if (!name || name === menu.previewPlaylistName)
                continue
            rows.push({ name: name })
        }
        return rows
    }

    function openVersion(row, playlistName) {
        if (!row || !row.versionId)
            return
        // Clear first so a second open of the same version still notifies C++.
        openVersionRequest.value = ""
        openVersionRequest.value = JSON.stringify({
            versionId: row.versionId,
            name: row.label || "",
            playlistName: playlistName || "",
            select: true,
            frameRange: row.frameRange || ""
        })
    }

    function openInCurrent(row) {
        openVersion(row, currentPlaylistName())
    }

    function previewVersion(row) {
        if (!row || !row.versionId)
            return
        previewVersionRequest.value = ""
        previewVersionRequest.value = JSON.stringify({
            versionId: row.versionId,
            name: row.label || "",
            frameRange: row.frameRange || ""
        })
    }

    XsPopupMenu {
        id: versionMenu
        visible: false
        menu_model_name: "mikros_version_menu_" + versionMenu
        property var versionRow: null
        property var playlistEntries: []
        property bool playlistMenuReady: false
        property var panelContext: helpers.contextPanel(versionMenu)

        function samePlaylists(left, right) {
            if (left.length !== right.length)
                return false
            for (let i = 0; i < left.length; i++) {
                if (left[i].name !== right[i].name)
                    return false
            }
            return true
        }

        function refreshPlaylists() {
            const root = theSessionData.playlistsRootIdx
            if (!root || !root.valid)
                return
            const next = menu.sessionPlaylists()
            if (!samePlaylists(playlistEntries, next))
                playlistEntries = next
            playlistMenuReady = true
        }

        function showFor(row, item, x, y) {
            versionRow = row
            refreshPlaylists()
            showMenu(item, x, y)
        }

        Component.onCompleted: refreshPlaylists()

        Connections {
            target: theSessionData
            function onRowsInserted() { versionMenu.refreshPlaylists() }
            function onRowsRemoved() { versionMenu.refreshPlaylists() }
            function onModelReset() { versionMenu.refreshPlaylists() }
            function onDataChanged() { versionMenu.refreshPlaylists() }
        }
    }

    Loader {
        active: versionMenu.playlistMenuReady && versionMenu.playlistEntries.length === 0
        sourceComponent: Component {
            Item {
                XsMenuModelItem {
                    text: "Add version to playlist"
                    menuItemType: "button"
                    menuPath: ""
                    menuItemPosition: 1
                    menuModelName: versionMenu.menu_model_name
                    enabled: false
                    panelContext: versionMenu.panelContext
                }
            }
        }
    }

    Repeater {
        model: versionMenu.playlistEntries
        Item {
            XsMenuModelItem {
                text: modelData.name
                menuItemType: "button"
                menuPath: "Add version to playlist"
                menuItemPosition: index
                menuModelName: versionMenu.menu_model_name
                panelContext: versionMenu.panelContext
                onActivated: menu.openVersion(versionMenu.versionRow, modelData.name)
                Component.onCompleted: {
                    if (index === 0)
                        setMenuPathPosition("Add version to playlist", 1)
                }
            }
        }
    }

    XsMenuModelItem {
        text: "Add version to new playlist"
        menuItemType: "button"
        menuPath: ""
        menuItemPosition: 2
        menuModelName: versionMenu.menu_model_name
        panelContext: versionMenu.panelContext
        onActivated: menu.openVersion(versionMenu.versionRow, "")
    }

    function showFor(row, item, x, y) {
        versionMenu.showFor(row, item, x, y)
    }
}
