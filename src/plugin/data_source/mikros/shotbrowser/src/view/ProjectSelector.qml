// SPDX-License-Identifier: Apache-2.0
import QtQuick

import xStudio 1.0
import xstudio.qml.models 1.0

// The project combo shows the name and writes the id. None writes -1.
Item {
    id: selector

    readonly property string noneLabel: "None"

    function parseRows(value) {
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

    function namesOf(rows) {
        if (!rows || !rows.length)
            return []
        let names = []
        for (let index = 0; index < rows.length; ++index)
            names.push(rows[index].name)
        return names
    }

    function withNone(names) {
        return [noneLabel].concat(names && names.length ? names : [])
    }

    function indexForId(rows, selectedId) {
        const id = Number(selectedId)
        if (!rows || !rows.length || !isFinite(id) || id < 0)
            return 0
        for (let index = 0; index < rows.length; ++index) {
            if (Number(rows[index].id) === id)
                return index + 1
        }
        return 0
    }

    function syncCombo() {
        const next = indexForId(rows, selected.value)
        if (projectCombo.currentIndex !== next)
            projectCombo.currentIndex = next
    }

    function deferSync() {
        Qt.callLater(syncCombo)
    }

    XsModuleData {
        id: attributes
        modelDataName: "mikros_shotbrowser_attributes"
    }

    XsAttributeValue {
        id: projects
        attributeTitle: "Projects"
        model: attributes
    }

    XsAttributeValue {
        id: selected
        attributeTitle: "SelectedProject"
        model: attributes
    }

    readonly property var rows: parseRows(projects.value)

    XsText {
        anchors.fill: parent
        visible: selector.rows.length === 0
        text: "Loading…"
        color: XsStyleSheet.hintColor
        font.pixelSize: XsStyleSheet.fontSize * 1.2
        font.weight: Font.Medium
        verticalAlignment: Text.AlignVCenter
    }

    XsComboBox {
        id: projectCombo
        anchors.fill: parent
        visible: selector.rows.length > 0
        font.weight: Font.Black
        placeholderText: "Set Project..."
        model: selector.withNone(selector.namesOf(selector.rows))
        Component.onCompleted: selector.deferSync()
        onModelChanged: selector.deferSync()
        onActivated: function(index) {
            const id = index <= 0 || index > selector.rows.length ? -1 : selector.rows[index - 1].id
            selected.value = String(id)
        }
        Connections {
            target: selected
            function onValueChanged() {
                selector.deferSync()
            }
        }
    }
}
