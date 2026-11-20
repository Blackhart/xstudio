// SPDX-License-Identifier: Apache-2.0
import QtQuick

import xStudio 1.0
import xstudio.qml.models 1.0

// Which version cards to show, the server total footer, load-more bumps, and the loaded flag.
Item {
    id: versions

    property string resultMode: "versions"
    property int switcherHeight: XsStyleSheet.widgetStdHeight + 4
    property int switcherSpacing: 2

    signal resultChosen(string mode)
    signal openRequested(var row)
    signal menuRequested(var row, var item, real x, real y)

    function versionListPlaceholder() {
        const projectId = String(selectedProject.value ?? "")
        if (!projectId || projectId === "-1")
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

    XsAttributeValue {
        id: selectedProject
        attributeTitle: "SelectedProject"
        model: attributes
    }

    XsAttributeValue {
        id: versionCardsAttr
        attributeTitle: "VersionCards"
        model: attributes
    }

    CatalogValue { id: footer; title: "VersionListFooter"; source: attributes }

    XsAttributeValue {
        id: versionsLoaded
        attributeTitle: "VersionsLoaded"
        model: attributes
    }

    XsAttributeValue {
        id: loadMoreVersions
        attributeTitle: "LoadMoreVersions"
        model: attributes
    }

    XsAttributeValue {
        id: versionSearch
        attributeTitle: "VersionSearch"
        model: attributes
    }

    property int loadMoreToken: 0
    property var cardRows: []

    // Watch the attribute without putting contentY in the `names:` binding (binding loop).
    property var cardsSource: versionCardsAttr.value
    onCardsSourceChanged: reloadCardRows()

    // Hold scroll on append / thumbs; reset to top on clear or first page.
    function reloadCardRows() {
        const rows = versionCardRows()
        const previousCount = Array.isArray(cardRows) ? cardRows.length : 0
        const nextCount = rows.length
        if (nextCount === 0 || previousCount === 0)
            versionColumn.resetScrollToTop()
        else
            versionColumn.holdScrollAt(versionColumn.contentYForHold())
        cardRows = rows
    }

    NameColumn {
        id: versionColumn
        anchors.fill: parent
        resultSwitcher: true
        switcherHeight: versions.switcherHeight
        switcherSpacing: versions.switcherSpacing
        resultMode: versions.resultMode
        onResultChosen: function(mode) { versions.resultChosen(mode) }
        loadedFooter: true
        customFooter: true
        footerText: footer.value
        listColor: XsStyleSheet.panelBgColor
        names: versions.cardRows
        infiniteScroll: true
        onLoadMoreRequested: {
            versions.loadMoreToken += 1
            loadMoreVersions.value = String(versions.loadMoreToken)
        }
        onSearchSubmitted: function(text) {
            versionSearch.value = text
        }
        placeholder: {
            selectedProject.value
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
