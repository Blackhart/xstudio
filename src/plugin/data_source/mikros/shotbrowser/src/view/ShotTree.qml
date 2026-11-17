// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts

import xStudio 1.0
import xstudio.qml.models 1.0

// Sequences and shots. Unfold, selection, and the search filter live here.
Item {
    id: tree

    property string query: ""
    property var openSequences: ({})

    function listPlaceholder() {
        if (!project.value)
            return "No project selected"
        if (!shotTreeNodes().length)
            return "Loading…"
        return "No results"
    }

    function shotTreeNodes() {
        let value = shotTreeAttr.value
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

    function toggleSequence(name) {
        const opening = !openSequences[name]
        const next = Object.assign({}, openSequences)
        next[name] = opening
        openSequences = next
        if (opening)
            expandedSequence.value = name
        else if (expandedSequence.value === name)
            expandedSequence.value = ""
    }

    function shotTreeRows() {
        const nodes = shotTreeNodes()
        const text = query.toLowerCase()
        const rows = []
        for (let i = 0; i < nodes.length; i++) {
            const name = String(nodes[i].name)
            const shots = nodes[i].shots && nodes[i].shots.length ? nodes[i].shots : []
            const shotNames = []
            for (let s = 0; s < shots.length; s++)
                shotNames.push(String(shots[s]))
            const shotsLoaded = !!nodes[i].shotsLoaded
            const nameHit = !text || name.toLowerCase().indexOf(text) !== -1
            const matchedShots = []
            for (let s = 0; s < shotNames.length; s++) {
                if (!text || shotNames[s].toLowerCase().indexOf(text) !== -1)
                    matchedShots.push(shotNames[s])
            }
            if (text && !nameHit && matchedShots.length === 0)
                continue
            const expanded = !!openSequences[name] || (text !== "" && !nameHit && matchedShots.length > 0)
            rows.push({
                name: name,
                depth: 0,
                parent: !shotsLoaded || shotNames.length > 0,
                open: expanded
            })
            if (!expanded)
                continue
            const visibleShots = text && !nameHit ? matchedShots : shotNames
            for (let s = 0; s < visibleShots.length; s++) {
                rows.push({
                    name: visibleShots[s],
                    depth: 1,
                    parent: false,
                    open: false
                })
            }
        }
        return rows
    }

    function chooseNode(name, depth) {
        if (depth === 0)
            sequence.value = sequence.value === name ? "" : name
        else
            shot.value = shot.value === name ? "" : name
    }

    XsModuleData {
        id: attributes
        modelDataName: "mikros_shotbrowser_attributes"
    }

    CatalogValue { id: project; title: "Project"; source: attributes }
    CatalogValue { id: sequence; title: "Sequence"; source: attributes }
    CatalogValue { id: shot; title: "Shot"; source: attributes }

    XsAttributeValue {
        id: shotTreeAttr
        attributeTitle: "ShotTree"
        model: attributes
    }

    XsAttributeValue {
        id: expandedSequence
        attributeTitle: "ExpandedSequence"
        model: attributes
    }

    Connections {
        target: project
        function onValueChanged() {
            tree.openSequences = ({})
        }
    }

    NameColumn {
        anchors.fill: parent
        title: ""
        tree: true
        listColor: XsStyleSheet.panelBgColor
        names: tree.shotTreeRows()
        placeholder: tree.listPlaceholder()
        rowDelegate: shotRow
    }

    Component {
        id: shotRow

        Rectangle {
            id: line
            required property var modelData

            readonly property string label: {
                if (modelData === undefined || modelData === null)
                    return ""
                if (modelData.name !== undefined)
                    return String(modelData.name)
                return String(modelData)
            }
            readonly property int depth: modelData && modelData.depth !== undefined ? modelData.depth : 0
            readonly property bool isParent: modelData && modelData.parent !== undefined ? modelData.parent : false
            readonly property bool isOpen: modelData && modelData.open !== undefined ? modelData.open : false
            readonly property real chevronWidth: XsStyleSheet.widgetStdHeight / 1.2
            readonly property bool chosen: depth === 0
                    ? label === sequence.value
                    : label === shot.value

            width: ListView.view.width
            height: XsStyleSheet.widgetStdHeight
            color: chosen
                   ? Qt.darker(XsStyleSheet.accentColor, 2)
                   : rowHover.hovered
                     ? XsStyleSheet.widgetBgNormalColor
                     : "transparent"
            border.width: 1
            border.color: rowHover.hovered ? XsStyleSheet.accentColor : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: line.depth === 0 ? 0 : (line.depth + 1) * line.chevronWidth
                spacing: 1

                XsSecondaryButton {
                    visible: line.depth === 0
                    Layout.preferredWidth: visible ? line.chevronWidth : 0
                    Layout.preferredHeight: line.height
                    Layout.maximumHeight: line.height
                    imgSrc: "qrc:/icons/chevron_right.svg"
                    isActive: line.isOpen
                    enabled: line.isParent
                    opacity: enabled ? 1 : 0.5
                    imageDiv.rotation: line.isOpen ? 90 : 0
                    onClicked: tree.toggleSequence(line.label)
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
                        onTapped: tree.chooseNode(line.label, line.depth)
                    }
                }
            }

            HoverHandler {
                id: rowHover
            }
        }
    }
}
