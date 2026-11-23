// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

import xStudio 1.0
import xstudio.qml.models 1.0

// The project combo shows the name and writes the id. None writes -1.
// Checked statuses and types are the projects the combo shows.
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

    // A checked row leaves the list. An unchecked row joins it.
    function toggleShown(attribute, label, checked) {
        const current = parseRows(attribute.value)
        let next = []
        for (let index = 0; index < current.length; ++index) {
            if (current[index] !== label)
                next.push(current[index])
        }
        if (!checked)
            next.push(label)
        attribute.value = next
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

    XsAttributeValue {
        id: statusOptionsAttr
        attributeTitle: "ProjectStatusOptions"
        model: attributes
    }

    XsAttributeValue {
        id: typeOptionsAttr
        attributeTitle: "ProjectTypeOptions"
        model: attributes
    }

    XsAttributeValue {
        id: shownStatusesAttr
        attributeTitle: "ShownStatuses"
        model: attributes
    }

    XsAttributeValue {
        id: shownTypesAttr
        attributeTitle: "ShownTypes"
        model: attributes
    }

    readonly property var rows: parseRows(projects.value)
    readonly property var statusOptions: parseRows(statusOptionsAttr.value)
    readonly property var typeOptions: parseRows(typeOptionsAttr.value)
    readonly property var shownStatuses: parseRows(shownStatusesAttr.value)
    readonly property var shownTypes: parseRows(shownTypesAttr.value)
    readonly property bool waitingForProjects: rows.length === 0
        && statusOptions.length === 0
        && typeOptions.length === 0

    RowLayout {
        anchors.fill: parent
        spacing: 1

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            XsText {
                anchors.fill: parent
                visible: selector.waitingForProjects
                text: "Loading…"
                color: XsStyleSheet.hintColor
                font.pixelSize: XsStyleSheet.fontSize * 1.2
                font.weight: Font.Medium
                verticalAlignment: Text.AlignVCenter
            }

            XsComboBox {
                id: projectCombo
                anchors.fill: parent
                visible: !selector.waitingForProjects
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

        XsPrimaryButton {
            id: filterBtn
            Layout.preferredWidth: XsStyleSheet.primaryButtonStdWidth * 0.8
            Layout.fillHeight: true
            imgSrc: "qrc:/icons/filter.svg"
            toolTip: "Project filters"
            isActive: selector.shownStatuses.length > 0 || selector.shownTypes.length > 0
            onClicked: {
                if (projectFilterPopup.visible)
                    projectFilterPopup.visible = false
                else
                    projectFilterPopup.showMenu(filterBtn, width / 2, height / 2)
            }
        }
    }

    XsPopupMenu {
        id: projectFilterPopup
        menu_model_name: "mikros_project_filter_popup" + projectFilterPopup
        visible: false
        closePolicy: filterBtn.hovered
            ? Popup.CloseOnEscape
            : Popup.CloseOnEscape | Popup.CloseOnPressOutside

        XsMenuModelItem {
            menuItemType: "divider"
            text: "Status"
            menuItemPosition: 1
            menuPath: ""
            menuModelName: projectFilterPopup.menu_model_name
        }

        Repeater {
            model: selector.statusOptions
            Item {
                XsMenuModelItem {
                    text: modelData
                    menuItemType: "toggle"
                    menuPath: ""
                    menuItemPosition: index + 2
                    menuModelName: projectFilterPopup.menu_model_name
                    isChecked: selector.shownStatuses.indexOf(modelData) !== -1
                    onActivated: selector.toggleShown(shownStatusesAttr, modelData, isChecked)
                }
            }
        }

        XsMenuModelItem {
            menuItemType: "divider"
            text: "Type"
            menuItemPosition: 1000
            menuPath: ""
            menuModelName: projectFilterPopup.menu_model_name
        }

        Repeater {
            model: selector.typeOptions
            Item {
                XsMenuModelItem {
                    text: modelData
                    menuItemType: "toggle"
                    menuPath: ""
                    menuItemPosition: index + 1001
                    menuModelName: projectFilterPopup.menu_model_name
                    isChecked: selector.shownTypes.indexOf(modelData) !== -1
                    onActivated: selector.toggleShown(shownTypesAttr, modelData, isChecked)
                }
            }
        }
    }
}
