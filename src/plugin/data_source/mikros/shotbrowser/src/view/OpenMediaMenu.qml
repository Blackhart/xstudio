// SPDX-License-Identifier: Apache-2.0
import QtQuick

import xStudio 1.0
import xstudio.qml.helpers 1.0
import xstudio.qml.models 1.0

// The "Add version to playlist" menu, and the OpenVersion write that follows a choice.
Item {
    id: menu

    property string openToken: ""

    function playlistUuidOf(index) {
        if (!index || !index.valid)
            return ""
        let target = index
        if (theSessionData.getPlaylistIndex)
            target = theSessionData.getPlaylistIndex(index)
        if (!target || !target.valid)
            return ""
        if (theSessionData.get(target, "typeRole") !== "Playlist")
            return ""
        const uuid = theSessionData.get(target, "actorUuidRole")
        if (uuid === undefined || uuid === null)
            return ""
        const text = String(uuid)
        if (text === "" || text === "{00000000-0000-0000-0000-000000000000}")
            return ""
        return text
    }

    function currentPlaylistUuid() {
        return playlistUuidOf(theSessionData.currentMediaContainerIndex)
    }

    function ensurePlaylist(uuid) {
        if (uuid)
            return uuid
        const created = theSessionData.createPlaylist(theSessionData.getNextName("Playlist {}"))
        return playlistUuidOf(created)
    }

    function sessionPlaylists() {
        const root = theSessionData.playlistsRootIdx
        const rows = []
        if (!root || !root.valid)
            return rows
        const count = theSessionData.rowCount(root)
        for (let i = 0; i < count; i++) {
            const idx = theSessionData.index(i, 0, root)
            const uuid = playlistUuidOf(idx)
            if (!uuid)
                continue
            rows.push({
                name: String(theSessionData.get(idx, "nameRole")),
                uuid: uuid
            })
        }
        return rows
    }

    function openVersion(row, playlistUuid, select) {
        if (!row || !row.versionId)
            return
        const uuid = select ? ensurePlaylist(playlistUuid) : playlistUuid
        if (!uuid)
            return
        const token = Date.now()
        openToken = String(token)
        openVersionRequest.value = JSON.stringify({
            token: token,
            select: !!select,
            versionId: row.versionId,
            name: row.label,
            playlistUuid: uuid,
            frameRange: row.frameRange
        })
    }

    function openInCurrent(row) {
        openVersion(row, currentPlaylistUuid(), true)
    }

    function applyOpenedMedia(value) {
        if (!value)
            return
        let data = value
        if (typeof value === "string") {
            try {
                data = JSON.parse(value)
            } catch (error) {
                return
            }
        }
        if (!data || !data.select || String(data.token) !== openToken)
            return
        const playlist = theSessionData.searchRecursive(data.playlistUuid, "actorUuidRole")
        if (!playlist || !playlist.valid)
            return
        mediaSelectionModel.selectFirstNewMedia(
            playlist, [helpers.QVariantFromUuidString(data.mediaUuid)])
    }

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
        id: openedMedia
        attributeTitle: "OpenedMedia"
        model: attributes
        onValueChanged: menu.applyOpenedMedia(value)
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
                if (left[i].uuid !== right[i].uuid || left[i].name !== right[i].name)
                    return false
            }
            return true
        }

        // Read the session before the menu opens, so the entry is already
        // at its position when the popup is shown.
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
                onActivated: menu.openVersion(versionMenu.versionRow, modelData.uuid, false)
                Component.onCompleted: {
                    // Keep the submenu on the same row as the disabled entry.
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
        onActivated: {
            const created = theSessionData.createPlaylist(
                theSessionData.getNextName("Playlist {}"))
            menu.openVersion(versionMenu.versionRow, menu.playlistUuidOf(created), false)
        }
    }

    function showFor(row, item, x, y) {
        versionMenu.showFor(row, item, x, y)
    }
}
