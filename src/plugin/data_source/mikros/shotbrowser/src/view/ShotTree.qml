// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import xStudio 1.0
import xstudio.qml.models 1.0
import mikros.shotbrowser.tree 1.0

// Sequences and shots. TreeView shows the C++ model; selection writes ids.
Item {
    id: tree

    property string query: ""
    property bool rebuildView: false ///< True after a clear, so the next pass drops the stale flat list.
    readonly property real chevronWidth: XsStyleSheet.widgetStdHeight / 1.2

    onQueryChanged: {
        ShotTreeBridge.filterText = query
        rebuildView = !query ///< A clear keeps expanded search rows; the timer rebuilds the view.
        expandAfterFilter.restart() ///< Filter layout settles before expand or fold.
    }

    function listPlaceholder() {
        const projectId = String(selectedProject.value ?? "")
        if (!projectId || projectId === "-1")
            return "No project selected"
        if (!shotTreeLoaded.value)
            return "Loading…"
        return "No results"
    }

    function chooseNode(nodeId, isSequence) {
        const key = String(nodeId)
        if (isSequence)
            sequence.value = String(sequence.value) === key ? "-1" : key
        else
            shot.value = String(shot.value) === key ? "-1" : key
    }

    function rowNodeId(row) {
        return String(shotTree.model.data(shotTree.index(row, 0), Qt.UserRole + 1))
    }

    function rowIsSequence(row) {
        return shotTree.model.data(shotTree.index(row, 0), Qt.UserRole + 3) === true
    }

    function expandMatchingSequences() {
        for (let row = 0; row < shotTree.rows; ++row) {
            if (!shotTree.isExpanded(row))
                shotTree.expand(row) ///< Sequences with a shot hit open; leaves stay put.
        }
    }

    function rebuildCollapsedView() {
        if (!rebuildView)
            return
        rebuildView = false ///< A later page reset must not reassign the model, or the timer loops.
        const model = shotTree.model
        shotTree.model = null ///< Drops the flat list left open by the search.
        shotTree.model = model ///< Same proxy; treeModel does not change for the life of the panel.
    }

    function expandSelectedShotSequence() {
        const shotId = String(shot.value ?? "-1")
        const sequenceId = String(sequence.value ?? "-1")
        if (shotId === "-1" || sequenceId === "-1")
            return ///< No selected shot: every sequence stays folded.
        for (let row = 0; row < shotTree.rows; ++row) {
            if (!rowIsSequence(row))
                continue ///< Shots inserted by expand sit on the following rows.
            if (rowNodeId(row) !== sequenceId)
                continue
            if (!shotTree.isExpanded(row))
                shotTree.expand(row) ///< Keep the selected shot's sequence open.
        }
    }

    function expandVisibleParents() {
        if (query) {
            expandMatchingSequences()
            return
        }
        rebuildCollapsedView() ///< Clear only; a page reset has already folded the tree.
        expandSelectedShotSequence()
    }

    Component.onCompleted: {
        ShotTreeBridge.filterText = query
        rebuildView = !query ///< First show with an empty search starts from a folded tree.
        expandAfterFilter.restart()
    }

    Timer {
        id: expandAfterFilter
        interval: 1
        repeat: false
        onTriggered: tree.expandVisibleParents()
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

    CatalogValue { id: sequence; title: "Sequence"; source: attributes }
    CatalogValue { id: shot; title: "Shot"; source: attributes }
    CatalogValue { id: footer; title: "ShotTreeFooter"; source: attributes }

    XsAttributeValue {
        id: shotTreeLoaded
        attributeTitle: "ShotTreeLoaded"
        model: attributes
    }

    // A page resets expansion. Search reopens matches; an empty search reopens the selected sequence.
    Connections {
        target: ShotTreeBridge.treeModel
        function onRowsInserted() {
            if (tree.query)
                expandAfterFilter.restart() ///< New rows during a search must open like the ones already shown.
        }
        function onModelReset() {
            expandAfterFilter.restart() ///< Do not set rebuildView: this reset already folded the tree.
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: XsStyleSheet.panelPadding

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: XsStyleSheet.panelBgColor

            XsText {
                anchors.centerIn: parent
                visible: shotTree.rows === 0
                text: {
                    selectedProject.value
                    shotTreeLoaded.value
                    return tree.listPlaceholder()
                }
                color: XsStyleSheet.secondaryTextColor
            }

            TreeView {
                id: shotTree
                anchors.fill: parent
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickDeceleration: 5000
                model: ShotTreeBridge.treeModel
                rowHeightProvider: function (row) { return XsStyleSheet.widgetStdHeight }
                // Search only. An empty query must not restart: opening the selected sequence changes rows.
                onRowsChanged: {
                    if (tree.query)
                        expandAfterFilter.restart() ///< Filter remaps rows; expand once the view has the new roots.
                }

                ScrollBar.vertical: XsScrollBar {}

                delegate: TreeViewDelegate {
                    id: line

                    readonly property string nodeId: model.nodeId !== undefined ? String(model.nodeId) : "-1"
                    readonly property string label: model.name !== undefined ? String(model.name) : String(model.display)
                    readonly property bool isSequence: !!model.isSequence
                    readonly property bool chosen: isSequence
                            ? line.nodeId === String(sequence.value)
                            : line.nodeId === String(shot.value)

                    implicitHeight: XsStyleSheet.widgetStdHeight
                    indentation: tree.chevronWidth
                    // TreeViewDelegate skips depth indent for leaves (!isTreeNode). Pad by hand.
                    leftMargin: 0
                    rightMargin: 0
                    leftPadding: 0
                    rightPadding: 0
                    topPadding: 0
                    bottomPadding: 0
                    spacing: 0
                    indicator: Item { width: 0; height: 0 }

                    background: Rectangle {
                        color: line.chosen
                               ? Qt.darker(XsStyleSheet.accentColor, 2)
                               : rowHover.hovered
                                 ? XsStyleSheet.widgetBgNormalColor
                                 : "transparent"
                        border.width: 1
                        border.color: rowHover.hovered ? XsStyleSheet.accentColor : "transparent"
                    }

                    contentItem: RowLayout {
                        spacing: 1
                        width: line.width
                        height: line.height

                        // TreeViewDelegate does not indent leaves; match the old NameColumn offset.
                        Item {
                            Layout.preferredWidth: line.depth === 0
                                    ? 0
                                    : (line.depth + 1) * tree.chevronWidth
                            Layout.preferredHeight: 1
                        }

                        XsSecondaryButton {
                            visible: line.isSequence
                            Layout.preferredWidth: visible ? tree.chevronWidth : 0
                            Layout.preferredHeight: line.height
                            Layout.maximumHeight: line.height
                            imgSrc: "qrc:/icons/chevron_right.svg"
                            isActive: line.expanded
                            enabled: line.isTreeNode
                            opacity: enabled ? 1 : 0.5
                            imageDiv.rotation: line.expanded ? 90 : 0
                            onClicked: {
                                if (line.expanded)
                                    shotTree.collapse(line.row)
                                else
                                    shotTree.expand(line.row)
                            }
                        }

                        XsText {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            leftPadding: 2
                            rightPadding: 8
                            text: line.label
                            color: XsStyleSheet.primaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize * 1.2
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight

                            TapHandler {
                                onTapped: tree.chooseNode(line.nodeId, line.isSequence)
                            }
                        }
                    }

                    HoverHandler {
                        id: rowHover
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 2
            Layout.maximumHeight: 2
            color: XsStyleSheet.panelBgColor
        }

        XsText {
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.preferredHeight: implicitHeight
            Layout.maximumHeight: implicitHeight
            text: footer.value
            color: "white"
            font.pixelSize: XsStyleSheet.fontSize * 0.8
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            rightPadding: XsStyleSheet.panelPadding
        }
    }
}
