// SPDX-License-Identifier: Apache-2.0
import QtQuick

import xStudio 1.0
import xstudio.qml.models 1.0

// Which version cards to show, the list footer, and the flag that the load is done.
Item {
    id: versions

    property string resultMode: "versions"
    property int switcherHeight: XsStyleSheet.widgetStdHeight + 4
    property int switcherSpacing: 2

    signal resultChosen(string mode)
    signal openRequested(var row)
    signal menuRequested(var row, var item, real x, real y)

    function versionListPlaceholder() {
        if (!project.value)
            return "No project selected"
        if (!versionsLoaded.value)
            return "Loading…"
        return "No results"
    }

    function versionCardRows() {
        let value = versionCardsAttr.value
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

    XsModuleData {
        id: attributes
        modelDataName: "mikros_shotbrowser_attributes"
    }

    CatalogValue { id: project; title: "Project"; source: attributes }

    XsAttributeValue {
        id: versionCardsAttr
        attributeTitle: "VersionCards"
        model: attributes
    }

    XsAttributeValue {
        id: versionsLoaded
        attributeTitle: "VersionsLoaded"
        model: attributes
    }

    NameColumn {
        anchors.fill: parent
        resultSwitcher: true
        switcherHeight: versions.switcherHeight
        switcherSpacing: versions.switcherSpacing
        resultMode: versions.resultMode
        onResultChosen: function(mode) { versions.resultChosen(mode) }
        loadedFooter: true
        footerNoun: "version"
        listColor: XsStyleSheet.panelBgColor
        names: versions.versionCardRows()
        placeholder: {
            project.value
            versionsLoaded.value
            return versions.versionListPlaceholder()
        }
        rowDelegate: versionRow
    }

    Component {
        id: versionRow

        VersionCard {
            id: card
            onOpenCurrent: versions.openRequested(card.payload())
            onMenuRequested: function(x, y) {
                versions.menuRequested(card.payload(), card, x, y)
            }
        }
    }
}
