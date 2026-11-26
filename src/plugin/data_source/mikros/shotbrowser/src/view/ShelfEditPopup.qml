// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import xStudio 1.0
import xstudio.qml.models 1.0

// Editor for one user shelf: ShotGrid-style filter groups (match all/any).
XsPopup {
    id: root
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: popupWidth
    height: popupHeight
    padding: XsStyleSheet.panelPadding
    repositioned: true ///< Keep the position set by openFor.
    lockPosition: true ///< Drag and resize must not re-anchor the popup.

    property real popupWidth: 520
    property real popupHeight: 440
    readonly property real minPopupWidth: 400
    readonly property real minPopupHeight: 320
    readonly property real defaultPopupWidth: 520
    readonly property real defaultPopupHeight: 440

    property var shelf: ({
        id: "new",
        name: "",
        filters: {}
    })
    property string formError: "" ///< Inline validation under the title.
    property string rootMatch: "all" ///< all → and, any → or between same-entity groups.

    signal saved(var shelf)

    XsPreference {
        id: widthPref
        path: "/plugin/data_source/mikros_shotbrowser/shelf_edit_popup_width"
    }

    XsPreference {
        id: heightPref
        path: "/plugin/data_source/mikros_shotbrowser/shelf_edit_popup_height"
    }

    // Apply the saved size, or the built-in default when none is stored.
    function restoreSize() {
        const width = Number(widthPref.value)
        const height = Number(heightPref.value)
        popupWidth = width >= minPopupWidth ? width : defaultPopupWidth
        popupHeight = height >= minPopupHeight ? height : defaultPopupHeight
    }

    // Persist the current size for the next open across runtimes.
    function rememberSize() {
        widthPref.value = popupWidth
        heightPref.value = popupHeight
    }

    XsModuleData {
        id: attributes
        modelDataName: "mikros_shotbrowser_attributes"
    }

    XsAttributeValue {
        id: shelfFields
        attributeTitle: "ShelfFields"
        model: attributes
    }

    ListModel { id: groupModel } ///< Rows: entity, match ("all"|"any"), termsJson.

    function fieldList() {
        const raw = shelfFields.value
        if (Array.isArray(raw))
            return raw
        return []
    }

    function entities() {
        const names = []
        const fields = root.fieldList()
        for (let i = 0; i < fields.length; i++) {
            const name = fields[i].entity
            if (name && names.indexOf(name) < 0)
                names.push(name)
        }
        return names
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

    function displayValue(value, op) {
        if (Array.isArray(value)) {
            if (op === "between")
                return value.join(" .. ")
            return value.join(", ")
        }
        if (value && typeof value === "object")
            return String(value.count) + " " + String(value.unit || "")
        if (value === undefined || value === null)
            return ""
        return String(value)
    }

    function encodeValue(entity, key, op, text) {
        const raw = String(text).trim()
        if (op === "in" || op === "not_in") {
            const parts = raw.split(",").map(function(part) { return part.trim() })
                .filter(function(part) { return part.length > 0 })
            if (!parts.length)
                return null
            if (root.fieldType(entity, key) === "int") {
                const nums = []
                for (let i = 0; i < parts.length; i++) {
                    if (!/^-?\d+$/.test(parts[i]))
                        return null
                    nums.push(Number(parts[i]))
                }
                return nums
            }
            return parts
        }
        if (op === "between") {
            const parts = raw.split("..").map(function(part) { return part.trim() })
            if (parts.length !== 2 || !parts[0] || !parts[1])
                return null
            const type = root.fieldType(entity, key)
            if (type === "int") {
                if (!/^-?\d+$/.test(parts[0]) || !/^-?\d+$/.test(parts[1]))
                    return null
                const low = Number(parts[0])
                const high = Number(parts[1])
                if (low > high)
                    return null
                return [low, high]
            }
            if (type === "date" || type === "datetime") {
                if (!/^\d{4}-\d{2}-\d{2}$/.test(parts[0]) || !/^\d{4}-\d{2}-\d{2}$/.test(parts[1]))
                    return null
                if (parts[0] > parts[1])
                    return null
                return parts
            }
            return null
        }
        if (op === "in_last") {
            const match = /^(\d+)\s+(HOUR|DAY|WEEK|MONTH|YEAR)$/.exec(raw)
            if (!match || Number(match[1]) < 1)
                return null
            return { count: Number(match[1]), unit: match[2] }
        }
        if (op === "in_calendar_day" || op === "in_calendar_week" || op === "in_calendar_month") {
            if (!/^-?\d+$/.test(raw))
                return null
            return Number(raw)
        }
        const type = root.fieldType(entity, key)
        if (type === "datetime" && (op === "eq" || op === "ne" || op === "gt" || op === "lt")) {
            if (!/^\d{4}-\d{2}-\d{2}$/.test(raw))
                return null
            return raw
        }
        if (type === "int") {
            if (!/^-?\d+$/.test(raw))
                return null
            return Number(raw)
        }
        return raw
    }

    function matchToOp(match) {
        return match === "any" ? "or" : "and"
    }

    function opToMatch(op) {
        return op === "or" ? "any" : "all"
    }

    function emptyCondition(entity) {
        const keys = root.fieldKeysFor(entity)
        const key = keys.length ? keys[0] : "name"
        const ops = root.opsFor(entity, key)
        return { key: key, op: ops[0] || "eq", value: "" }
    }

    function emptyGroup(entity) {
        const name = entity || "version"
        return {
            entity: name,
            match: "all",
            termsJson: JSON.stringify([root.emptyCondition(name)])
        }
    }

    function groupTerms(groupIndex) {
        const row = groupModel.get(groupIndex)
        if (!row)
            return []
        try {
            const parsed = JSON.parse(row.termsJson || "[]")
            return Array.isArray(parsed) ? parsed : []
        } catch (e) {
            return []
        }
    }

    function setGroupTerms(groupIndex, terms) {
        groupModel.setProperty(groupIndex, "termsJson", JSON.stringify(terms))
    }

    function addFilterGroup() {
        groupModel.append(root.emptyGroup("version"))
    }

    function removeFilterGroup(groupIndex) {
        if (groupModel.count <= 1) {
            groupModel.set(0, root.emptyGroup(groupModel.get(0).entity))
            return
        }
        groupModel.remove(groupIndex)
    }

    function addCondition(groupIndex) {
        const entity = groupModel.get(groupIndex).entity
        const terms = root.groupTerms(groupIndex)
        terms.push(root.emptyCondition(entity))
        root.setGroupTerms(groupIndex, terms)
    }

    function removeCondition(groupIndex, termIndex) {
        const terms = root.groupTerms(groupIndex)
        terms.splice(termIndex, 1)
        if (!terms.length)
            terms.push(root.emptyCondition(groupModel.get(groupIndex).entity))
        root.setGroupTerms(groupIndex, terms)
    }

    function setConditionProperty(groupIndex, termIndex, prop, value) {
        const terms = root.groupTerms(groupIndex)
        terms[termIndex][prop] = value
        if (prop === "key") {
            const entity = groupModel.get(groupIndex).entity
            const ops = root.opsFor(entity, value)
            terms[termIndex].op = ops[0] || "eq"
        }
        root.setGroupTerms(groupIndex, terms)
    }

    function setGroupEntity(groupIndex, entity) {
        const terms = root.groupTerms(groupIndex)
        groupModel.setProperty(groupIndex, "entity", entity)
        for (let i = 0; i < terms.length; i++) {
            const keys = root.fieldKeysFor(entity)
            terms[i].key = keys.length ? keys[0] : "name"
            const ops = root.opsFor(entity, terms[i].key)
            terms[i].op = ops[0] || "eq"
            terms[i].value = ""
        }
        root.setGroupTerms(groupIndex, terms)
    }

    function fieldLeafFromCondition(entity, term) {
        const value = root.encodeValue(entity, term.key, term.op, term.value)
        if (value === null)
            return null
        return { kind: "field", op: term.op, key: term.key, value: value }
    }

    function treeFromGroup(entity, match, terms) {
        const leaves = []
        for (let i = 0; i < terms.length; i++) {
            if (terms[i].value === "" || terms[i].value === undefined || terms[i].value === null)
                continue
            const leaf = root.fieldLeafFromCondition(entity, terms[i])
            if (leaf === null)
                return { error: true }
            leaves.push(leaf)
        }
        if (!leaves.length)
            return null
        if (leaves.length === 1)
            return leaves[0]
        return { kind: "group", op: root.matchToOp(match), args: leaves }
    }

    function buildFilters() {
        const byEntity = {}
        for (let g = 0; g < groupModel.count; g++) {
            const row = groupModel.get(g)
            const entity = row.entity
            if (entity === "project")
                continue
            const tree = root.treeFromGroup(entity, row.match, root.groupTerms(g))
            if (tree && tree.error) {
                formError = "Check the value for a condition."
                return null
            }
            if (!tree)
                continue
            if (!byEntity[entity])
                byEntity[entity] = []
            byEntity[entity].push(tree)
        }
        const filters = {}
        const entityKeys = Object.keys(byEntity)
        for (let e = 0; e < entityKeys.length; e++) {
            const entity = entityKeys[e]
            const trees = byEntity[entity]
            if (trees.length === 1)
                filters[entity] = trees[0]
            else
                filters[entity] = {
                    kind: "group",
                    op: root.matchToOp(rootMatch),
                    args: trees
                }
        }
        return filters
    }

    function conditionFromField(entity, node) {
        return {
            key: node.key || "name",
            op: node.op || "eq",
            value: root.displayValue(node.value, node.op || "eq")
        }
    }

    function flattenLeaves(node, out) {
        if (!node || typeof node !== "object")
            return
        if (node.kind === "field") {
            out.push(node)
            return
        }
        if (node.kind === "group" && Array.isArray(node.args)) {
            for (let i = 0; i < node.args.length; i++)
                root.flattenLeaves(node.args[i], out)
        }
    }

    function groupsFromFilters(filters) {
        const groups = []
        let loadedRootMatch = "all"
        if (!filters || typeof filters !== "object")
            return { rootMatch: loadedRootMatch, groups: [root.emptyGroup("version")] }

        const entityNames = root.entities()
        for (let e = 0; e < entityNames.length; e++) {
            const entity = entityNames[e]
            const tree = filters[entity]
            if (!tree || typeof tree !== "object")
                continue
            if (tree.kind === "field") {
                groups.push({
                    entity: entity,
                    match: "all",
                    termsJson: JSON.stringify([root.conditionFromField(entity, tree)])
                })
                continue
            }
            if (tree.kind !== "group" || !Array.isArray(tree.args))
                continue

            const args = tree.args
            const allFields = args.every(function(a) { return a && a.kind === "field" })
            if (allFields) {
                groups.push({
                    entity: entity,
                    match: root.opToMatch(tree.op),
                    termsJson: JSON.stringify(args.map(function(a) {
                        return root.conditionFromField(entity, a)
                    }))
                })
                continue
            }

            // Two-level: root op joins subgroups / leaves.
            loadedRootMatch = root.opToMatch(tree.op)
            for (let i = 0; i < args.length; i++) {
                const arg = args[i]
                if (arg.kind === "field") {
                    groups.push({
                        entity: entity,
                        match: "all",
                        termsJson: JSON.stringify([root.conditionFromField(entity, arg)])
                    })
                } else if (arg.kind === "group" && Array.isArray(arg.args)
                           && arg.args.every(function(a) { return a && a.kind === "field" })) {
                    groups.push({
                        entity: entity,
                        match: root.opToMatch(arg.op),
                        termsJson: JSON.stringify(arg.args.map(function(a) {
                            return root.conditionFromField(entity, a)
                        }))
                    })
                } else {
                    const leaves = []
                    root.flattenLeaves(arg, leaves)
                    groups.push({
                        entity: entity,
                        match: "all",
                        termsJson: JSON.stringify(leaves.map(function(a) {
                            return root.conditionFromField(entity, a)
                        }))
                    })
                }
            }
        }
        if (!groups.length)
            groups.push(root.emptyGroup("version"))
        return { rootMatch: loadedRootMatch, groups: groups }
    }

    function valuePlaceholder(entity, fieldKey, op) {
        if (op === "in" || op === "not_in")
            return "ip, rev, wtg"
        if (op === "between") {
            if (root.fieldType(entity, fieldKey) === "int")
                return "0 .. 5"
            return "2024-01-01 .. 2024-03-01"
        }
        if (op === "in_last")
            return "7 DAY"
        if (op === "in_calendar_day" || op === "in_calendar_week" || op === "in_calendar_month")
            return "0"
        if (root.fieldType(entity, fieldKey) === "datetime")
            return "YYYY-MM-DD"
        if (root.fieldType(entity, fieldKey) === "entity" || fieldKey === "author")
            return "e.g. Ada"
        if (fieldKey === "name")
            return "e.g. paw17_sq010"
        if (root.fieldType(entity, fieldKey) === "int")
            return "e.g. 1"
        return "Filter value"
    }

    function opHelp(entity, fieldKey, op) {
        const type = root.fieldType(entity, fieldKey)
        const example = root.valuePlaceholder(entity, fieldKey, op)
        let meaning = ""
        let value = ""
        let format = ""
        if (op === "eq") {
            meaning = "Matches when the field equals the value."
            if (type === "datetime") {
                value = "A calendar day"
                format = "YYYY-MM-DD"
            } else if (type === "int") {
                value = "An integer"
                format = "digits only"
            } else {
                value = "Exact text"
                format = "free text"
            }
        } else if (op === "ne") {
            meaning = "Matches when the field differs from the value."
            if (type === "datetime") {
                value = "A calendar day"
                format = "YYYY-MM-DD"
            } else if (type === "int") {
                value = "An integer"
                format = "digits only"
            } else {
                value = "Exact text"
                format = "free text"
            }
        } else if (op === "contains") {
            meaning = "Matches when the field contains the substring (case-insensitive)."
            value = "A substring"
            format = "free text"
        } else if (op === "not_contains") {
            meaning = "Matches when the field does not contain the substring."
            value = "A substring to exclude"
            format = "free text"
        } else if (op === "starts_with") {
            meaning = "Matches when the field starts with the prefix."
            value = "A prefix"
            format = "free text"
        } else if (op === "ends_with") {
            meaning = "Matches when the field ends with the suffix."
            value = "A suffix"
            format = "free text"
        } else if (op === "in") {
            meaning = "Matches when the field is one of the listed values."
            value = "A non-empty list"
            format = type === "int" ? "integers separated by commas" : "values separated by commas"
        } else if (op === "not_in") {
            meaning = "Matches when the field is none of the listed values."
            value = "A non-empty list"
            format = type === "int" ? "integers separated by commas" : "values separated by commas"
        } else if (op === "gt") {
            meaning = "Matches when the field is greater than the value."
            if (type === "datetime" || type === "date") {
                value = "A calendar day"
                format = "YYYY-MM-DD"
            } else {
                value = "An integer"
                format = "digits only"
            }
        } else if (op === "lt") {
            meaning = "Matches when the field is less than the value."
            if (type === "datetime" || type === "date") {
                value = "A calendar day"
                format = "YYYY-MM-DD"
            } else {
                value = "An integer"
                format = "digits only"
            }
        } else if (op === "between") {
            meaning = "Matches when the field lies in the inclusive range."
            value = "Lower and upper bounds"
            format = type === "int" ? "low .. high (integers)" : "YYYY-MM-DD .. YYYY-MM-DD"
        } else if (op === "in_last") {
            meaning = "Matches when the datetime falls in the past period."
            value = "A positive count and a unit"
            format = "N HOUR|DAY|WEEK|MONTH|YEAR"
        } else if (op === "in_calendar_day") {
            meaning = "Matches a day offset from today (0 = today, -1 = yesterday)."
            value = "An integer offset"
            format = "signed integer"
        } else if (op === "in_calendar_week") {
            meaning = "Matches a week offset from this week (0 = this week, -1 = last week)."
            value = "An integer offset"
            format = "signed integer"
        } else if (op === "in_calendar_month") {
            meaning = "Matches a month offset from this month (0 = this month, -1 = last month)."
            value = "An integer offset"
            format = "signed integer"
        } else if (op === "name_contains") {
            meaning = "Matches when the linked entity name contains the substring."
            value = "A substring of the person or entity name"
            format = "free text"
        } else if (op === "name_starts_with") {
            meaning = "Matches when the linked entity name starts with the prefix."
            value = "A name prefix"
            format = "free text"
        } else if (op === "name_is") {
            meaning = "Matches when the linked entity name equals the value."
            value = "Exact linked name"
            format = "free text"
        } else {
            meaning = "Filter operator."
            value = "See the value field"
            format = "depends on the field"
        }
        return meaning + "\nValue: " + value + "\nFormat: " + format + "\nExample: " + example
    }

    function openFor(refItem, existing) {
        formError = ""
        restoreSize()
        shelf = existing ? Object.assign({}, existing) : {
            id: "new",
            name: "",
            filters: {}
        }
        nameField.text = shelf.name || ""
        const loaded = root.groupsFromFilters(shelf.filters)
        rootMatch = loaded.rootMatch
        rootMatchCombo.currentIndex = Math.max(0, ["all", "any"].indexOf(rootMatch))
        groupModel.clear()
        for (let i = 0; i < loaded.groups.length; i++)
            groupModel.append(loaded.groups[i])
        if (refItem)
            root.showMenu(refItem, refItem.width / 2, refItem.height / 2)
        else
            root.open()
    }

    contentItem: Item {
        implicitWidth: 500
        implicitHeight: 400

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: XsStyleSheet.panelPadding
            spacing: 8

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: titleLabel.implicitHeight

                XsText {
                    id: titleLabel
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: shelf.id === "new" ? "New shelf" : "Edit shelf"
                    font.pixelSize: XsStyleSheet.fontSize * 1.1
                    font.weight: Font.Medium
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeAllCursor
                    property real pressGlobalX
                    property real pressGlobalY
                    property real originX
                    property real originY
                    onPressed: function(mouse) {
                        const global = mapToGlobal(mouse.x, mouse.y)
                        pressGlobalX = global.x
                        pressGlobalY = global.y
                        originX = root.x
                        originY = root.y
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed)
                            return
                        const global = mapToGlobal(mouse.x, mouse.y)
                        root.x = originX + (global.x - pressGlobalX)
                        root.y = originY + (global.y - pressGlobalY)
                    }
                }
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
                    text: "Match"
                    Layout.alignment: Qt.AlignVCenter
                }

                XsComboBox {
                    id: rootMatchCombo
                    Layout.preferredWidth: 70
                    Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                    model: ["all", "any"]
                    onActivated: rootMatch = currentText
                }

                XsText {
                    text: "of the following groups"
                    Layout.alignment: Qt.AlignVCenter
                }

                Item { Layout.fillWidth: true }
            }

            ListView {
                id: groupList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 8
                model: groupModel

                delegate: Rectangle {
                    id: groupCard
                    width: ListView.view.width
                    height: groupColumn.implicitHeight + 8
                    color: XsStyleSheet.panelBgColor
                    border.width: 1
                    border.color: XsStyleSheet.baseColor

                    required property int index
                    required property string entity
                    required property string match
                    required property string termsJson

                    readonly property int groupIndex: index
                    readonly property string groupEntity: entity
                    property bool writingTerms: false ///< Skip reload while pushing local edits up.

                    ListModel { id: localTerms }

                    function reloadLocalTerms() {
                        localTerms.clear()
                        const terms = root.groupTerms(groupIndex)
                        for (let i = 0; i < terms.length; i++)
                            localTerms.append(terms[i])
                    }

                    function pushLocalTerms() {
                        const terms = []
                        for (let i = 0; i < localTerms.count; i++) {
                            const row = localTerms.get(i)
                            terms.push({ key: row.key, op: row.op, value: row.value })
                        }
                        writingTerms = true
                        root.setGroupTerms(groupIndex, terms)
                        writingTerms = false
                    }

                    onTermsJsonChanged: {
                        if (!writingTerms)
                            reloadLocalTerms()
                    }
                    Component.onCompleted: reloadLocalTerms()

                    ColumnLayout {
                        id: groupColumn
                        width: parent.width - 8
                        x: 4
                        y: 4
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                            spacing: 4

                            XsText {
                                text: "Filter group"
                                Layout.alignment: Qt.AlignVCenter
                            }

                            XsComboBox {
                                Layout.preferredWidth: 100
                                Layout.preferredHeight: parent.height
                                readonly property var entityChoices: root.entities()
                                model: entityChoices
                                currentIndex: Math.max(0, entityChoices.indexOf(groupCard.groupEntity))
                                onActivated: root.setGroupEntity(groupCard.groupIndex, currentText)
                            }

                            XsText {
                                text: "Match"
                                Layout.alignment: Qt.AlignVCenter
                            }

                            XsComboBox {
                                Layout.preferredWidth: 70
                                Layout.preferredHeight: parent.height
                                model: ["all", "any"]
                                currentIndex: Math.max(0, ["all", "any"].indexOf(groupCard.match))
                                onActivated: groupModel.setProperty(groupCard.groupIndex, "match", currentText)
                            }

                            Item { Layout.fillWidth: true }

                            XsPrimaryButton {
                                Layout.preferredWidth: XsStyleSheet.widgetStdHeight
                                Layout.preferredHeight: parent.height
                                imgSrc: "qrc:/icons/delete.svg"
                                toolTip: "Remove filter group"
                                onClicked: root.removeFilterGroup(groupCard.groupIndex)
                            }
                        }

                        Repeater {
                            model: localTerms

                            delegate: RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                                spacing: 4

                                required property int index
                                required property string key
                                required property string op
                                required property var value

                                readonly property int termIndex: index

                                XsComboBox {
                                    Layout.preferredWidth: 110
                                    Layout.preferredHeight: parent.height
                                    readonly property var keyChoices: root.fieldKeysFor(groupCard.groupEntity)
                                    model: keyChoices
                                    currentIndex: Math.max(0, keyChoices.indexOf(key))
                                    onActivated: {
                                        localTerms.setProperty(termIndex, "key", currentText)
                                        const ops = root.opsFor(groupCard.groupEntity, currentText)
                                        localTerms.setProperty(termIndex, "op", ops[0] || "eq")
                                        groupCard.pushLocalTerms()
                                    }
                                }

                                XsComboBox {
                                    Layout.preferredWidth: 140
                                    Layout.preferredHeight: parent.height
                                    readonly property var opChoices: root.opsFor(groupCard.groupEntity, key)
                                    model: opChoices
                                    currentIndex: Math.max(0, opChoices.indexOf(op))
                                    onActivated: {
                                        localTerms.setProperty(termIndex, "op", currentText)
                                        groupCard.pushLocalTerms()
                                    }
                                }

                                XsInfoButton {
                                    Layout.preferredWidth: XsStyleSheet.widgetStdHeight
                                    Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                                    maxWidth: root.width * 0.7
                                    tooltipText: root.opHelp(groupCard.groupEntity, key, op)
                                }

                                XsTextField {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: parent.height
                                    text: value
                                    placeholderText: root.valuePlaceholder(
                                        groupCard.groupEntity, key, op)
                                    onTextEdited: {
                                        localTerms.setProperty(termIndex, "value", text)
                                        groupCard.pushLocalTerms()
                                        formError = ""
                                    }
                                    onEditingFinished: {
                                        localTerms.setProperty(termIndex, "value", text)
                                        groupCard.pushLocalTerms()
                                    }
                                }

                                XsPrimaryButton {
                                    Layout.preferredWidth: XsStyleSheet.widgetStdHeight
                                    Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                                    imgSrc: "qrc:/icons/delete.svg"
                                    toolTip: "Remove condition"
                                    onClicked: {
                                        root.removeCondition(groupCard.groupIndex, termIndex)
                                    }
                                }
                            }
                        }

                        XsPrimaryButton {
                            Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                            imgSrc: "qrc:/icons/add.svg"
                            toolTip: "Add condition"
                            text: "Condition"
                            onClicked: root.addCondition(groupCard.groupIndex)
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
                    toolTip: "Add filter group"
                    text: "Filter group"
                    onClicked: root.addFilterGroup()
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
                        if (!name) {
                            formError = "Enter a shelf name."
                            return
                        }
                        const filters = root.buildFilters()
                        if (filters === null)
                            return
                        if (!Object.keys(filters).length) {
                            formError = "Add at least one condition."
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

        MouseArea {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: 16
            height: 16
            cursorShape: Qt.SizeFDiagCursor
            z: 10
            property real pressGlobalX
            property real pressGlobalY
            property real originW
            property real originH
            onPressed: function(mouse) {
                const global = mapToGlobal(mouse.x, mouse.y)
                pressGlobalX = global.x
                pressGlobalY = global.y
                originW = root.popupWidth
                originH = root.popupHeight
            }
            onPositionChanged: function(mouse) {
                if (!pressed)
                    return
                const global = mapToGlobal(mouse.x, mouse.y)
                root.popupWidth = Math.max(root.minPopupWidth, originW + (global.x - pressGlobalX))
                root.popupHeight = Math.max(root.minPopupHeight, originH + (global.y - pressGlobalY))
            }
            onReleased: root.rememberSize()
        }
    }
}
