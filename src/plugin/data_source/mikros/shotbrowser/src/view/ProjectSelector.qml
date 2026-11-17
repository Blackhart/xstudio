// SPDX-License-Identifier: Apache-2.0
import QtQuick

import xStudio 1.0
import xstudio.qml.models 1.0

// The project combo, including the wait state before the catalog arrives.
Item {
    id: selector

    readonly property string noneLabel: "None"

    function withNone(options) {
        return [noneLabel].concat(options && options.length ? options : [])
    }

    function indexForSelection(options, selected) {
        if (!selected)
            return 0
        const index = options ? options.indexOf(selected) : -1
        return index >= 0 ? index + 1 : 0
    }

    function valueFromIndex(options, index) {
        if (index <= 0 || !options || index > options.length)
            return ""
        return options[index - 1]
    }

    function syncCombo(options, selected) {
        const next = indexForSelection(options, selected)
        if (projectCombo.currentIndex !== next)
            projectCombo.currentIndex = next
    }

    function deferSync() {
        Qt.callLater(function() {
            syncCombo(project.names, project.value)
        })
    }

    XsModuleData {
        id: attributes
        modelDataName: "mikros_shotbrowser_attributes"
    }

    CatalogValue {
        id: project
        title: "Project"
        source: attributes
    }

    XsText {
        anchors.fill: parent
        visible: project.names.length === 0
        text: "Loading…"
        color: XsStyleSheet.hintColor
        font.pixelSize: XsStyleSheet.fontSize * 1.2
        font.weight: Font.Medium
        verticalAlignment: Text.AlignVCenter
    }

    XsComboBox {
        id: projectCombo
        anchors.fill: parent
        visible: project.names.length > 0
        font.weight: Font.Black
        placeholderText: "Set Project..."
        model: selector.withNone(project.names)
        Component.onCompleted: selector.deferSync()
        onModelChanged: selector.deferSync()
        onActivated: function(index) {
            project.value = selector.valueFromIndex(project.names, index)
        }
        Connections {
            target: project
            function onValueChanged() {
                selector.deferSync()
            }
        }
    }
}
