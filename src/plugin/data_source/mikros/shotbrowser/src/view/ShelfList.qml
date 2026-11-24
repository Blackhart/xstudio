// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts

import xStudio 1.0
import xstudio.qml.models 1.0

// Studio + User shelves (mediaBrowser-like). All lives under Studio.
Item {
    id: shelves

    XsModuleData {
        id: attributes
        modelDataName: "mikros_shotbrowser_attributes"
    }

    XsAttributeValue {
        id: shelvesAttr
        attributeTitle: "Shelves"
        model: attributes
    }

    XsAttributeValue {
        id: selectedShelf
        attributeTitle: "SelectedShelf"
        model: attributes
    }

    XsAttributeValue {
        id: selectedProject
        attributeTitle: "SelectedProject"
        model: attributes
    }

    XsAttributeValue {
        id: upsertShelf
        attributeTitle: "UpsertShelf"
        model: attributes
    }

    XsAttributeValue {
        id: removeShelf
        attributeTitle: "RemoveShelf"
        model: attributes
    }

    function shelfRows() {
        const raw = shelvesAttr.value
        if (Array.isArray(raw))
            return raw
        return []
    }

    function rowsFor(section) {
        return shelves.shelfRows().filter(function(row) {
            return row.section === section
        })
    }

    function treeRows() {
        const rows = []
        function pushSection(title, section, withAdd) {
            rows.push({
                kind: "header",
                name: title,
                id: "hdr-" + section,
                depth: 0,
                parent: false,
                open: false,
                editable: false,
                showAdd: !!withAdd
            })
            const items = shelves.rowsFor(section)
            if (!items.length && section === "studio") {
                rows.push({
                    kind: "empty",
                    name: "None",
                    id: "empty-" + section,
                    depth: 1,
                    parent: false,
                    open: false,
                    editable: false,
                    showAdd: false
                })
            }
            for (let i = 0; i < items.length; i++) {
                const row = items[i]
                rows.push({
                    kind: "shelf",
                    id: row.id,
                    name: row.name,
                    filters: row.filters || {},
                    editable: !!row.editable,
                    section: section,
                    depth: 1,
                    parent: false,
                    open: false,
                    showAdd: false
                })
            }
        }
        pushSection("Studio", "studio", false)
        pushSection("User", "user", true)
        return rows
    }

    function listPlaceholder() {
        const projectId = String(selectedProject.value ?? "")
        if (!projectId || projectId === "-1")
            return "No project selected"
        return "No shelves"
    }

    function chooseShelf(id) {
        if (!id)
            return
        // Always select (do not toggle off): All must stay active like project All.
        selectedShelf.value = id
    }

    ShelfEditPopup {
        id: editor
        onSaved: function(shelf) {
            upsertShelf.value = shelf
        }
    }

    NameColumn {
        anchors.fill: parent
        title: ""
        tree: true
        listColor: XsStyleSheet.panelBgColor
        names: {
            shelvesAttr.value
            return shelves.treeRows()
        }
        placeholder: {
            selectedProject.value
            return shelves.listPlaceholder()
        }
        rowDelegate: shelfRow
    }

    Component {
        id: shelfRow

        Rectangle {
            id: line
            required property var modelData

            readonly property string kind: modelData && modelData.kind !== undefined
                                           ? String(modelData.kind) : "shelf"
            readonly property string shelfId: modelData && modelData.id !== undefined
                                              ? String(modelData.id) : ""
            readonly property string label: modelData && modelData.name !== undefined
                                            ? String(modelData.name) : ""
            readonly property bool editable: !!(modelData && modelData.editable)
            readonly property bool showAdd: !!(modelData && modelData.showAdd)
            readonly property bool isHeader: kind === "header"
            readonly property bool isEmpty: kind === "empty"
            readonly property bool isShelf: kind === "shelf"
            readonly property int depth: modelData && modelData.depth !== undefined
                                         ? modelData.depth : 0
            readonly property real indent: depth * (XsStyleSheet.widgetStdHeight / 1.2)
            readonly property bool chosen: isShelf && shelfId === String(selectedShelf.value ?? "")
            readonly property bool showActions: isShelf && editable && rowHover.hovered

            width: ListView.view.width - (ListView.view.rightSpacing || 0)
            height: XsStyleSheet.widgetStdHeight
            color: chosen
                   ? Qt.darker(XsStyleSheet.accentColor, 2)
                   : (isShelf && rowHover.hovered
                      ? XsStyleSheet.widgetBgNormalColor
                      : "transparent")
            border.width: 1
            border.color: isShelf && rowHover.hovered ? XsStyleSheet.accentColor : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: line.indent
                spacing: 1

                XsText {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    leftPadding: 2
                    rightPadding: 8
                    text: line.label
                    color: line.isHeader || line.isEmpty
                           ? XsStyleSheet.hintColor
                           : XsStyleSheet.primaryTextColor
                    font.pixelSize: XsStyleSheet.fontSize * (line.isHeader ? 1.0 : 1.2)
                    font.weight: line.isHeader ? Font.Medium : Font.Normal
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    opacity: line.isEmpty ? 0.6 : 1

                    TapHandler {
                        enabled: line.isShelf
                        onTapped: shelves.chooseShelf(line.shelfId)
                    }
                }

                XsPrimaryButton {
                    id: addUserBtn
                    visible: line.isHeader && line.showAdd
                    Layout.preferredWidth: visible ? XsStyleSheet.widgetStdHeight : 0
                    Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                    imgSrc: "qrc:/icons/add.svg"
                    toolTip: "New user shelf"
                    z: 2
                    onClicked: editor.openFor(addUserBtn, null)
                }

                XsPrimaryButton {
                    visible: line.showActions
                    Layout.preferredWidth: visible ? XsStyleSheet.widgetStdHeight : 0
                    Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                    imgSrc: "qrc:/icons/edit.svg"
                    toolTip: "Edit shelf"
                    z: 2
                    onClicked: editor.openFor(this, {
                        id: line.shelfId,
                        name: line.label,
                        filters: (modelData && modelData.filters) || {}
                    })
                }

                XsPrimaryButton {
                    visible: line.showActions
                    Layout.preferredWidth: visible ? XsStyleSheet.widgetStdHeight : 0
                    Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                    imgSrc: "qrc:/icons/delete.svg"
                    toolTip: "Delete shelf"
                    z: 2
                    onClicked: removeShelf.value = line.shelfId
                }
            }

            HoverHandler {
                id: rowHover
                enabled: line.isShelf || (line.isHeader && line.showAdd)
            }
        }
    }
}
