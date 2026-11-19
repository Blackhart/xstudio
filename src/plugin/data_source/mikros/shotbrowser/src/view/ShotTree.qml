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
    readonly property real chevronWidth: XsStyleSheet.widgetStdHeight / 1.2

    onQueryChanged: {
        ShotTreeBridge.filterText = query
        expandAfterFilter.restart() ///< TreeView clears expansion on filter layout; reopen after it settles.
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

    function expandVisibleParents() {
        if (!query)
            return
        for (let row = 0; row < shotTree.rows; ++row) {
            if (!shotTree.isExpanded(row))
                shotTree.expand(row) ///< Sequences with a shot hit open; leaves stay put.
        }
    }

    Component.onCompleted: {
        ShotTreeBridge.filterText = query
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

    // Shots arriving while a search is active: reopen parents so matches stay visible.
    Connections {
        target: ShotTreeBridge.treeModel
        function onRowsInserted() {
            if (tree.query)
                expandAfterFilter.restart()
        }
        function onModelReset() {
            if (tree.query)
                expandAfterFilter.restart()
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
                // Filter remaps rows after invalidateFilter; expand once the view has the new roots.
                onRowsChanged: {
                    if (tree.query)
                        expandAfterFilter.restart()
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
