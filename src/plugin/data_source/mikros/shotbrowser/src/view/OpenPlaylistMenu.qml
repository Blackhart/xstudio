// SPDX-License-Identifier: Apache-2.0
import QtQuick

import xStudio 1.0
import xstudio.qml.helpers 1.0
import xstudio.qml.models 1.0

// Context menu for a ShotGrid playlist. Writes OpenPlaylist for the view model.
Item {
    id: menu

    XsModuleData {
        id: attributes
        modelDataName: "mikros_shotbrowser_attributes"
    }

    XsAttributeValue {
        id: openPlaylistRequest
        attributeTitle: "OpenPlaylist"
        model: attributes
    }

    function openPlaylist(row) {
        if (!row || !row.playlistId)
            return
        // Clear first so a second open of the same playlist still notifies C++.
        openPlaylistRequest.value = ""
        openPlaylistRequest.value = JSON.stringify({
            playlistId: row.playlistId,
            name: row.name || ""
        })
    }

    XsPopupMenu {
        id: playlistMenu
        visible: false
        menu_model_name: "mikros_playlist_menu_" + playlistMenu
        property var playlistRow: null
        property var panelContext: helpers.contextPanel(playlistMenu)

        function showFor(row, item, x, y) {
            playlistRow = row
            showMenu(item, x, y)
        }
    }

    XsMenuModelItem {
        text: "Open playlist"
        menuItemType: "button"
        menuPath: ""
        menuItemPosition: 1
        menuModelName: playlistMenu.menu_model_name
        panelContext: playlistMenu.panelContext
        onActivated: menu.openPlaylist(playlistMenu.playlistRow)
    }

    function showFor(row, item, x, y) {
        playlistMenu.showFor(row, item, x, y)
    }
}
