// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import xStudio 1.0
import xstudio.qml.models 1.0

// Editor for one user shelf: name and multi-entity FieldExpr terms.
XsPopup {
    id: root
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: 520
    height: 440
    padding: XsStyleSheet.panelPadding
    repositioned: true ///< Keep the position set by openFor.

    property var shelf: ({
        id: "new",
        name: "",
        filters: {}
    })
    property string formError: "" ///< Inline validation under the title.

    signal saved(var shelf)

    XsModuleData {
        id: attributes
        modelDataName: "mikros_shotbrowser_attributes"
    }

    XsAttributeValue {
        id: shelfFields
        attributeTitle: "ShelfFields"
        model: attributes
    }

    function fieldList() {
        const raw = shelfFields.value
        if (Array.isArray(raw))
            return raw
        return []
    }

    function entities() {
        return ["sequence", "shot", "asset", "version", "playlist"]
    }

    function fieldsFor(entity) {
        return root.fieldList().filter(function(f) { return f.entity === entity })
    }

    function fieldKeysFor(entity) {
        return root.fieldsFor(entity).map(function(f) { return f.key })
    }

    function opsFor(entity, fieldKey) {
        const fields = root.fieldsFor(entity)
        for (let i = 0; i < fields.length; i++) {
            if (fields[i].key === fieldKey)
                return fields[i].ops || ["eq"]
        }
        return ["eq"]
    }

    function fieldType(entity, fieldKey) {
        const fields = root.fieldsFor(entity)
        for (let i = 0; i < fields.length; i++) {
            if (fields[i].key === fieldKey)
                return fields[i].type || "string"
        }
        return "string"
    }

    function termsFromFilters(filters) {
        if (!filters || typeof filters !== "object")
            return [{ entity: "version", key: "name", op: "contains", value: "" }]
        const terms = []
        const entities = root.entities()
        for (let e = 0; e < entities.length; e++) {
            const entity = entities[e]
            const tree = filters[entity]
            if (!tree || typeof tree !== "object")
                continue
            if (tree.kind === "field") {
                terms.push({
                    entity: entity,
                    key: tree.key || "name",
                    op: tree.op || "eq",
                    value: tree.value ?? ""
                })
                continue
            }
            if (tree.kind === "group" && Array.isArray(tree.args)) {
                for (let i = 0; i < tree.args.length; i++) {
                    const arg = tree.args[i]
                    if (arg && arg.kind === "field") {
                        terms.push({
                            entity: entity,
                            key: arg.key || "name",
                            op: arg.op || "eq",
                            value: arg.value ?? ""
                        })
                    }
                }
            }
        }
        if (terms.length)
            return terms
        return [{ entity: "version", key: "name", op: "contains", value: "" }]
    }

    function groupOpFromFilters(filters) {
        if (!filters || typeof filters !== "object")
            return "and"
        const entities = root.entities()
        for (let e = 0; e < entities.length; e++) {
            const tree = filters[entities[e]]
            if (tree && tree.kind === "group" && (tree.op === "or" || tree.op === "and"))
                return tree.op
        }
        return "and"
    }

    function buildFilters() {
        const byEntity = {}
        for (let i = 0; i < termModel.count; i++) {
            const row = termModel.get(i)
            if (!row.entity || !row.key || row.value === "")
                continue
            let value = row.value
            if (root.fieldType(row.entity, row.key) === "int") {
                const n = Number(row.value)
                if (!Number.isNaN(n))
                    value = Math.trunc(n)
            }
            if (!byEntity[row.entity])
                byEntity[row.entity] = []
            byEntity[row.entity].push({ kind: "field", op: row.op, key: row.key, value: value })
        }
        const join = groupOp.currentText
        const filters = {}
        const keys = Object.keys(byEntity)
        for (let k = 0; k < keys.length; k++) {
            const entity = keys[k]
            const terms = byEntity[entity]
            if (terms.length === 1)
                filters[entity] = terms[0]
            else
                filters[entity] = { kind: "group", op: join, args: terms }
        }
        if (!Object.keys(filters).length)
            return {}
        return filters
    }

    function valuePlaceholder(entity, fieldKey) {
        if (fieldKey === "name")
            return "e.g. paw17_sq010"
        if (root.fieldType(entity, fieldKey) === "int")
            return "e.g. 1"
        return "Filter value"
    }

    function openFor(refItem, existing) {
        formError = ""
        shelf = existing ? Object.assign({}, existing) : {
            id: "new",
            name: "",
            filters: {}
        }
        nameField.text = shelf.name || ""
        const join = groupOpFromFilters(shelf.filters)
        const joins = ["and", "or"]
        groupOp.model = joins
        groupOp.currentIndex = Math.max(0, joins.indexOf(join))
        termModel.clear()
        const terms = termsFromFilters(shelf.filters)
        for (let i = 0; i < terms.length; i++)
            termModel.append(terms[i])
        if (refItem)
            root.showMenu(refItem, refItem.width / 2, refItem.height / 2)
        else
            root.open()
    }

    ListModel { id: termModel }

    contentItem: Item {
        implicitWidth: 500
        implicitHeight: 400

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: XsStyleSheet.panelPadding
            spacing: 8

            XsText {
                text: shelf.id === "new" ? "New shelf" : "Edit shelf"
                font.pixelSize: XsStyleSheet.fontSize * 1.1
                font.weight: Font.Medium
                Layout.fillWidth: true
            }

            XsText {
                visible: formError.length > 0
                text: formError
                color: XsStyleSheet.accentColor
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            XsTextField {
                id: nameField
                Layout.fillWidth: true
                Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                placeholderText: "Shelf name"
                onTextEdited: formError = ""
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                spacing: 8

                XsText {
                    text: "Join"
                    Layout.alignment: Qt.AlignVCenter
                }

                XsComboBox {
                    id: groupOp
                    Layout.preferredWidth: 90
                    Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                    model: ["and", "or"]
                }

                Item { Layout.fillWidth: true }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: XsStyleSheet.panelBgColor
                border.width: 1
                border.color: XsStyleSheet.baseColor

                ListView {
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    model: termModel
                    spacing: 4
                    delegate: RowLayout {
                        width: ListView.view.width
                        height: XsStyleSheet.widgetStdHeight
                        spacing: 4

                        required property int index
                        required property string entity
                        required property string key
                        required property string op
                        required property var value
                        readonly property int termIndex: index ///< List row; ComboBox.activated also has index.

                        XsComboBox {
                            Layout.preferredWidth: 100
                            Layout.preferredHeight: parent.height
                            readonly property var entityChoices: root.entities()
                            model: entityChoices
                            currentIndex: Math.max(0, entityChoices.indexOf(entity))
                            onActivated: function(comboIndex) {
                                const nextEntity = currentText
                                termModel.setProperty(termIndex, "entity", nextEntity)
                                const keys = root.fieldKeysFor(nextEntity)
                                const nextKey = keys.length ? keys[0] : "name"
                                termModel.setProperty(termIndex, "key", nextKey)
                                const ops = root.opsFor(nextEntity, nextKey)
                                termModel.setProperty(termIndex, "op", ops[0] || "eq")
                            }
                        }

                        XsComboBox {
                            Layout.preferredWidth: 110
                            Layout.preferredHeight: parent.height
                            readonly property var keyChoices: root.fieldKeysFor(entity)
                            model: keyChoices
                            currentIndex: Math.max(0, keyChoices.indexOf(key))
                            onActivated: function(comboIndex) {
                                termModel.setProperty(termIndex, "key", currentText)
                                const ops = root.opsFor(entity, currentText)
                                termModel.setProperty(termIndex, "op", ops[0] || "eq")
                            }
                        }

                        XsComboBox {
                            Layout.preferredWidth: 90
                            Layout.preferredHeight: parent.height
                            readonly property var opChoices: root.opsFor(entity, key)
                            model: opChoices
                            currentIndex: Math.max(0, opChoices.indexOf(op))
                            onActivated: function(comboIndex) {
                                termModel.setProperty(termIndex, "op", currentText)
                            }
                        }

                        XsTextField {
                            Layout.fillWidth: true
                            Layout.preferredHeight: parent.height
                            text: value
                            placeholderText: root.valuePlaceholder(entity, key)
                            // Write on each edit: Save often clicks before EditingFinished.
                            onTextEdited: {
                                termModel.setProperty(termIndex, "value", text)
                                formError = ""
                            }
                            onEditingFinished: termModel.setProperty(termIndex, "value", text)
                        }

                        XsPrimaryButton {
                            Layout.preferredWidth: XsStyleSheet.widgetStdHeight
                            Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                            imgSrc: "qrc:/icons/delete.svg"
                            toolTip: "Remove term"
                            onClicked: termModel.remove(termIndex)
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                spacing: 4

                XsPrimaryButton {
                    Layout.preferredHeight: parent.height
                    imgSrc: "qrc:/icons/add.svg"
                    toolTip: "Add term"
                    text: "Term"
                    onClicked: {
                        const entity = "version"
                        const keys = root.fieldKeysFor(entity)
                        const key = keys.length ? keys[0] : "name"
                        const ops = root.opsFor(entity, key)
                        termModel.append({
                            entity: entity,
                            key: key,
                            op: ops[0] || "eq",
                            value: ""
                        })
                    }
                }

                Item { Layout.fillWidth: true }

                XsPrimaryButton {
                    Layout.preferredHeight: parent.height
                    text: "Cancel"
                    onClicked: root.close()
                }

                XsPrimaryButton {
                    Layout.preferredHeight: parent.height
                    text: "Save"
                    onClicked: {
                        forceActiveFocus() ///< Leave the value field so the model is current.
                        const name = nameField.text.trim()
                        const filters = root.buildFilters()
                        if (!name) {
                            formError = "Enter a shelf name."
                            return
                        }
                        if (!Object.keys(filters).length) {
                            formError = "Add at least one filter with a value."
                            return
                        }
                        formError = ""
                        root.saved({
                            id: shelf.id || "new",
                            name: name,
                            filters: filters
                        })
                        root.close()
                    }
                }
            }
        }
    }
}
