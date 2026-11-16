// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import xStudio 1.0
import xstudio.qml.models 1.0

Item {
    id: panel
    anchors.fill: parent

    readonly property string noneLabel: "None"
    readonly property int rowHeight: XsStyleSheet.widgetStdHeight
    readonly property int tabHeight: XsStyleSheet.widgetStdHeight + 4
    readonly property int buttonSpacing: 1
    readonly property int panelPadding: XsStyleSheet.panelPadding
    readonly property real leftRatio: 0.3
    readonly property int splitMin: 80
    readonly property int splitFreeMax: 100000

    property string browserMode: "shot"
    property string treeQuery: ""
    property var openSequences: ({})
    property real leftPaneWidth: 0

    function restoreMode() {
        Qt.callLater(function() {
            const saved = modePref.value
            if (saved === "shot" || saved === "asset")
                browserMode = saved
        })
    }

    function restoreWidth() {
        Qt.callLater(function() {
            const width = Number(widthPref.value)
            if (width >= splitMin)
                leftPaneWidth = width
        })
    }

    function rememberWidth(width) {
        if (width < splitMin)
            return
        leftPaneWidth = width
        widthPref.value = width
    }

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

    function syncCombo(combo, options, selected) {
        const next = indexForSelection(options, selected)
        if (combo.currentIndex !== next)
            combo.currentIndex = next
    }

    function deferSync(combo, catalog) {
        Qt.callLater(function() {
            syncCombo(combo, catalog.names, catalog.value)
        })
    }

    function listPlaceholder(sourceCount) {
        if (!project.value)
            return "No project selected"
        if (!sourceCount)
            return "Loading…"
        return "No results"
    }

    function shotTreeNodes() {
        let value = shotTree.value
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
        const next = Object.assign({}, openSequences)
        next[name] = !next[name]
        openSequences = next
    }

    function shotTreeRows() {
        const nodes = shotTreeNodes()
        const query = treeQuery.toLowerCase()
        const rows = []
        for (let i = 0; i < nodes.length; i++) {
            const name = String(nodes[i].name)
            const shots = nodes[i].shots && nodes[i].shots.length ? nodes[i].shots : []
            const shotNames = []
            for (let s = 0; s < shots.length; s++)
                shotNames.push(String(shots[s]))
            const nameHit = !query || name.toLowerCase().indexOf(query) !== -1
            const matchedShots = []
            for (let s = 0; s < shotNames.length; s++) {
                if (!query || shotNames[s].toLowerCase().indexOf(query) !== -1)
                    matchedShots.push(shotNames[s])
            }
            if (query && !nameHit && matchedShots.length === 0)
                continue
            const expanded = !!openSequences[name] || (query !== "" && !nameHit && matchedShots.length > 0)
            rows.push({
                name: name,
                depth: 0,
                parent: shotNames.length > 0,
                open: expanded
            })
            if (!expanded)
                continue
            const visibleShots = query && !nameHit ? matchedShots : shotNames
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

    function assetTreeRows() {
        const labels = filteredNames(asset.names)
        const rows = []
        for (let i = 0; i < labels.length; i++) {
            rows.push({
                name: labels[i],
                depth: 0,
                parent: false,
                open: false
            })
        }
        return rows
    }

    function filteredNames(names) {
        if (!names || !names.length)
            return []
        if (!treeQuery)
            return names
        const query = treeQuery.toLowerCase()
        const matched = []
        for (let i = 0; i < names.length; i++) {
            if (String(names[i]).toLowerCase().indexOf(query) !== -1)
                matched.push(names[i])
        }
        return matched
    }

    // One catalog attribute: the current label and the choice list.
    component Catalog: Item {
        id: catalog
        visible: false
        width: 0
        height: 0

        required property string title
        required property var source

        property alias value: current.value
        readonly property var names: options.value && options.value.length ? options.value : []

        XsAttributeValue {
            id: current
            attributeTitle: catalog.title
            model: catalog.source
        }

        XsAttributeValue {
            id: options
            attributeTitle: catalog.title
            model: catalog.source
            role: "combo_box_options"
        }
    }

    // Row whose height cannot grow with the column.
    component Bar: RowLayout {
        property int barHeight: XsStyleSheet.widgetStdHeight
        spacing: 4
        Layout.fillWidth: true
        Layout.fillHeight: false
        Layout.preferredHeight: barHeight
        Layout.minimumHeight: barHeight
        Layout.maximumHeight: barHeight
    }

    component InfoCell: Rectangle {
        id: cell
        property string label: "N/A"
        property color labelColor: XsStyleSheet.hintColor
        property int hAlign: Text.AlignHCenter
        color: XsStyleSheet.widgetBgNormalColor

        XsText {
            anchors.fill: parent
            text: cell.label
            color: cell.labelColor
            font.pixelSize: XsStyleSheet.fontSize * 1.1
            horizontalAlignment: cell.hAlign
            verticalAlignment: Text.AlignVCenter
            leftPadding: cell.hAlign === Text.AlignLeft ? XsStyleSheet.panelPadding : 4
            rightPadding: 4
            elide: Text.ElideRight
        }
    }

    component NameColumn: ColumnLayout {
        id: column
        property string title
        property url iconSrc
        property var names
        property string selected
        property string placeholder
        property bool boxedCount: false
        property bool loadedFooter: false
        property color listColor: "transparent"
        property bool tree: false

        signal nodeToggled(string name)

        readonly property int count: names && names.length ? names.length : 0

        spacing: XsStyleSheet.panelPadding

        Bar {
            visible: column.title !== ""
            spacing: 1

            XsIcon {
                visible: column.iconSrc != ""
                Layout.preferredWidth: visible ? 16 : 0
                Layout.preferredHeight: 16
                source: column.iconSrc
            }

            XsText {
                text: column.title
                Layout.fillWidth: true
                font.pixelSize: XsStyleSheet.fontSize * 1.2
                font.weight: Font.Medium
            }

            XsText {
                visible: !column.loadedFooter && !column.boxedCount
                text: column.count
                opacity: 0.5
                font.pixelSize: XsStyleSheet.fontSize * 1.2
                horizontalAlignment: Text.AlignRight
            }

            Rectangle {
                visible: !column.loadedFooter && column.boxedCount
                Layout.preferredWidth: 72
                Layout.fillHeight: true
                color: XsStyleSheet.panelBgColor
                border.width: 1
                border.color: XsStyleSheet.menuBorderColor

                XsText {
                    anchors.centerIn: parent
                    text: column.count
                    font.pixelSize: XsStyleSheet.fontSize * 1.2
                }
            }
        }

        Rectangle {
            visible: column.title !== ""
            Layout.fillWidth: true
            Layout.preferredHeight: 2
            color: XsStyleSheet.panelBgColor
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: column.listColor

            XsLabel {
                anchors.fill: parent
                visible: column.count === 0
                text: column.placeholder
                color: XsStyleSheet.hintColor
                font.pixelSize: XsStyleSheet.fontSize * 1.2
                font.weight: Font.Medium
            }

            ListView {
                id: nameView
                anchors.fill: parent
                visible: column.count > 0
                clip: true
                spacing: column.tree ? 1 : XsStyleSheet.panelPadding
                model: column.names
                // Version cards are tall. The default flick keeps travelling
                // for a long way; DNEG's list stops almost immediately.
                flickDeceleration: column.tree ? 1500 : 100000
                maximumFlickVelocity: column.tree ? 2500 : 800
                boundsBehavior: column.tree ? Flickable.DragAndOvershootBounds : Flickable.StopAtBounds

                WheelHandler {
                    enabled: !column.tree
                    onWheel: function(event) {
                        let step = event.pixelDelta.y !== 0
                                   ? event.pixelDelta.y
                                   : (event.angleDelta.y / 120) * XsStyleSheet.widgetStdHeight
                        let limit = Math.max(0, nameView.contentHeight - nameView.height)
                        nameView.contentY = Math.max(0, Math.min(nameView.contentY - step, limit))
                        event.accepted = true
                    }
                }

                ScrollBar.vertical: XsScrollBar {
                    visible: nameView.height < nameView.contentHeight
                    policy: ScrollBar.AsNeeded
                }

                delegate: Rectangle {
                    id: row
                    required property var modelData

                    readonly property string label: column.tree ? modelData.name : String(modelData)
                    readonly property int depth: column.tree ? modelData.depth : 0
                    readonly property bool isParent: column.tree ? modelData.parent : false
                    readonly property bool isOpen: column.tree ? modelData.open : false
                    readonly property real chevronWidth: XsStyleSheet.widgetStdHeight / 1.2

                    width: nameView.width
                    height: column.tree
                            ? XsStyleSheet.widgetStdHeight
                            : (XsStyleSheet.widgetStdHeight + 1) * 4
                    color: column.tree
                           ? (label === column.selected
                              ? Qt.darker(XsStyleSheet.accentColor, 2)
                              : rowHover.hovered
                                ? XsStyleSheet.widgetBgNormalColor
                                : "transparent")
                           : (label === column.selected
                              ? Qt.darker(XsStyleSheet.accentColor, 5)
                              : "transparent")
                    border.width: 1
                    border.color: column.tree
                                   ? (rowHover.hovered ? XsStyleSheet.accentColor : "transparent")
                                   : (rowHover.hovered ? XsStyleSheet.accentColor : XsStyleSheet.widgetBgNormalColor)

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: row.depth === 0 ? 0 : (row.depth + 1) * row.chevronWidth
                        spacing: 1
                        visible: column.tree

                        XsSecondaryButton {
                            visible: row.depth === 0
                            Layout.preferredWidth: visible ? row.chevronWidth : 0
                            Layout.preferredHeight: row.height
                            Layout.maximumHeight: row.height
                            imgSrc: "qrc:/icons/chevron_right.svg"
                            isActive: row.isOpen
                            enabled: row.isParent
                            opacity: enabled ? 1 : 0.5
                            imageDiv.rotation: row.isOpen ? 90 : 0
                            onClicked: column.nodeToggled(row.label)
                        }

                        XsText {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            leftPadding: 2
                            rightPadding: 8
                            text: row.label
                            color: XsStyleSheet.primaryTextColor
                            font.pixelSize: XsStyleSheet.fontSize * 1.2
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 1
                        spacing: 1
                        visible: !column.tree

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                            Layout.maximumHeight: XsStyleSheet.widgetStdHeight
                            color: XsStyleSheet.widgetBgNormalColor

                            XsText {
                                anchors.fill: parent
                                leftPadding: XsStyleSheet.panelPadding
                                text: row.label
                                font.pixelSize: XsStyleSheet.fontSize * 1.1
                                font.bold: true
                                horizontalAlignment: Text.AlignLeft
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideMiddle
                            }

                            XsText {
                                anchors.fill: parent
                                rightPadding: XsStyleSheet.panelPadding
                                text: "N/A"
                                font.pixelSize: XsStyleSheet.fontSize * 1.1
                                font.bold: true
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignVCenter
                                opacity: 0.5
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 1

                            Rectangle {
                                Layout.preferredWidth: 154
                                Layout.maximumWidth: 154
                                Layout.fillHeight: true
                                color: XsStyleSheet.widgetBgNormalColor

                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 1

                                    ColumnLayout {
                                        Layout.preferredWidth: 20
                                        Layout.maximumWidth: 20
                                        Layout.fillHeight: true
                                        spacing: 1

                                        Repeater {
                                            model: ["N", "D", "C"]

                                            XsPrimaryButton {
                                                required property string modelData
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                                text: modelData
                                                enabled: false
                                                opacity: 0.5
                                                font.pixelSize: XsStyleSheet.fontSize * 1.2
                                                textDiv.color: XsStyleSheet.hintColor
                                            }
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        color: "black"

                                        Image {
                                            anchors.fill: parent
                                            anchors.margins: 4
                                            source: "qrc:/mikros_shotbrowser_icons/no_preview.jpg"
                                            fillMode: Image.PreserveAspectFit
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.minimumWidth: 115
                                spacing: 1

                                InfoCell {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                                    Layout.maximumHeight: XsStyleSheet.widgetStdHeight
                                    labelColor: XsStyleSheet.primaryTextColor
                                    hAlign: Text.AlignLeft
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                                    Layout.maximumHeight: XsStyleSheet.widgetStdHeight
                                    spacing: 1

                                    InfoCell {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        hAlign: Text.AlignLeft
                                    }

                                    InfoCell {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        hAlign: Text.AlignLeft
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    color: XsStyleSheet.widgetBgNormalColor

                                    XsText {
                                        anchors.fill: parent
                                        anchors.margins: XsStyleSheet.panelPadding
                                        text: "N/A"
                                        color: XsStyleSheet.primaryTextColor
                                        horizontalAlignment: Text.AlignLeft
                                        verticalAlignment: Text.AlignTop
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.preferredWidth: 157
                                Layout.maximumWidth: 157
                                Layout.fillHeight: true
                                spacing: 1

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                                    Layout.maximumHeight: XsStyleSheet.widgetStdHeight
                                    spacing: 1

                                    InfoCell {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        labelColor: XsStyleSheet.primaryTextColor
                                    }

                                    InfoCell {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                    }
                                }

                                InfoCell {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: XsStyleSheet.widgetStdHeight
                                    Layout.maximumHeight: XsStyleSheet.widgetStdHeight
                                    labelColor: XsStyleSheet.primaryTextColor
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: XsStyleSheet.widgetStdHeight - 1
                                    Layout.maximumHeight: XsStyleSheet.widgetStdHeight - 1
                                    spacing: 1

                                    Repeater {
                                        model: ["prs", "mtl", "bgl"]

                                        Rectangle {
                                            required property string modelData
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            color: Qt.lighter(XsStyleSheet.panelBgColor, 1.5)
                                            opacity: 0.5

                                            XsLabel {
                                                anchors.fill: parent
                                                text: modelData
                                                font.pixelSize: XsStyleSheet.fontSize / 1.4
                                                font.weight: Font.Medium
                                            }
                                        }
                                    }
                                }

                                Item {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                }
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
            visible: column.loadedFooter
            Layout.fillWidth: true
            Layout.preferredHeight: 2
            Layout.maximumHeight: 2
            color: XsStyleSheet.panelBgColor
        }

        XsText {
            visible: column.loadedFooter
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.preferredHeight: implicitHeight
            Layout.maximumHeight: implicitHeight
            text: column.count === 0 ? "No versions found"
                  : column.count === 1 ? "1 version loaded"
                  : column.count + " versions loaded"
            color: "white"
            font.pixelSize: XsStyleSheet.fontSize * 0.8
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            rightPadding: XsStyleSheet.panelPadding
        }
    }

    XsModuleData {
        id: shotbrowserAttributes
        modelDataName: "mikros_shotbrowser_attributes"
    }

    Catalog { id: project; title: "Project"; source: shotbrowserAttributes }
    Catalog { id: shot; title: "Shot"; source: shotbrowserAttributes }

    XsAttributeValue {
        id: shotTree
        attributeTitle: "ShotTree"
        model: shotbrowserAttributes
    }

    Connections {
        target: project
        function onValueChanged() {
            panel.openSequences = ({})
        }
    }

    XsPreference {
        id: modePref
        path: "/plugin/data_source/mikros_shotbrowser/browser_mode"
        onValueChanged: panel.restoreMode()
    }

    XsPreference {
        id: widthPref
        path: "/plugin/data_source/mikros_shotbrowser/left_pane_width"
        onValueChanged: panel.restoreWidth()
    }
    Catalog { id: asset; title: "Asset"; source: shotbrowserAttributes }
    Catalog { id: version; title: "Version"; source: shotbrowserAttributes }

    XsSplitView {
        id: mainSplit
        anchors.fill: parent

        // Until the user drags, or a saved width is restored, the left pane
        // stays at leftRatio. SplitView otherwise keeps the implicit width.
        property bool dragging: false
        readonly property bool widthSaved: panel.leftPaneWidth >= panel.splitMin
        readonly property bool widthFree: widthSaved || dragging
        readonly property real startupWidth: Math.max(1, width * panel.leftRatio)

        handle: Rectangle {
            id: splitHandle
            implicitWidth: XsStyleSheet.panelPadding / 2
            implicitHeight: XsStyleSheet.panelPadding / 2
            color: XsStyleSheet.panelBgColor

            Rectangle {
                anchors.fill: parent
                color: parent.SplitHandle.pressed ? XsStyleSheet.accentColor
                     : (parent.SplitHandle.hovered ? XsStyleSheet.primaryTextColor : "transparent")
            }

            property bool handlePressed: SplitHandle.pressed
            onHandlePressedChanged: {
                mainSplit.dragging = handlePressed
                if (!handlePressed && panel.leftPaneWidth >= panel.splitMin)
                    widthPref.value = panel.leftPaneWidth
            }
        }

        Item {
            id: leftPane
            SplitView.fillHeight: true
            SplitView.minimumWidth: panel.splitMin
            SplitView.preferredWidth: mainSplit.widthSaved ? panel.leftPaneWidth : mainSplit.startupWidth
            SplitView.maximumWidth: mainSplit.widthFree ? panel.splitFreeMax : mainSplit.startupWidth
            onWidthChanged: {
                if (mainSplit.dragging)
                    panel.leftPaneWidth = width
            }

            XsGradientRectangle {
                anchors.fill: parent
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: panel.panelPadding
                spacing: panel.panelPadding

                Bar {
                    barHeight: panel.tabHeight
                    spacing: panel.buttonSpacing

                    XsPrimaryButton {
                        Layout.preferredWidth: panel.tabHeight
                        Layout.fillHeight: true
                        imgSrc: "qrc:/icons/movie.svg"
                        toolTip: "Shot Tree View"
                        isActive: panel.browserMode === "shot"
                        onClicked: {
                            panel.browserMode = "shot"
                            modePref.value = "shot"
                        }
                    }

                    XsPrimaryButton {
                        Layout.preferredWidth: panel.tabHeight
                        Layout.fillHeight: true
                        imgSrc: "qrc:/icons/deployed_cube.svg"
                        toolTip: "Asset Tree View"
                        isActive: panel.browserMode === "asset"
                        onClicked: {
                            panel.browserMode = "asset"
                            modePref.value = "asset"
                        }
                    }

                    Item {
                        Layout.preferredWidth: XsStyleSheet.panelPadding * 4
                        Layout.fillHeight: true
                    }

                    XsText {
                        visible: project.names.length === 0
                        text: "Loading…"
                        Layout.fillWidth: true
                        color: XsStyleSheet.hintColor
                        font.pixelSize: XsStyleSheet.fontSize * 1.2
                        font.weight: Font.Medium
                    }

                    XsComboBox {
                        id: projectCombo
                        visible: project.names.length > 0
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        font.weight: Font.Black
                        placeholderText: "Set Project..."
                        model: withNone(project.names)
                        Component.onCompleted: panel.deferSync(projectCombo, project)
                        onModelChanged: panel.deferSync(projectCombo, project)
                        onActivated: function(index) {
                            project.value = valueFromIndex(project.names, index)
                        }
                        Connections {
                            target: project
                            function onValueChanged() {
                                panel.deferSync(projectCombo, project)
                            }
                        }
                    }

                    XsPrimaryButton {
                        Layout.preferredWidth: XsStyleSheet.primaryButtonStdWidth * 0.8
                        Layout.fillHeight: true
                        imgSrc: "qrc:/icons/filter.svg"
                        toolTip: "Status"
                        isActive: false
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 2
                    color: XsStyleSheet.panelBgColor
                }

                Bar {
                    barHeight: panel.tabHeight
                    spacing: panel.buttonSpacing

                    XsSearchBar {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        placeholderText: "Search..."
                        onTextChanged: panel.treeQuery = text
                    }

                    XsPrimaryButton {
                        Layout.preferredWidth: XsStyleSheet.primaryButtonStdWidth
                        Layout.fillHeight: true
                        imgSrc: "qrc:/icons/filter.svg"
                        toolTip: "Filter"
                        isActive: false
                    }
                }

                NameColumn {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: ""
                    tree: true
                    listColor: XsStyleSheet.panelBgColor
                    names: panel.browserMode === "shot" ? panel.shotTreeRows() : panel.assetTreeRows()
                    selected: panel.browserMode === "shot" ? shot.value : asset.value
                    placeholder: listPlaceholder(panel.browserMode === "shot" ? panel.shotTreeNodes().length : asset.names.length)
                    onNodeToggled: function(name) {
                        if (panel.browserMode === "shot")
                            panel.toggleSequence(name)
                    }
                }
            }
        }

        Item {
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            SplitView.minimumWidth: 500

            XsGradientRectangle {
                anchors.fill: parent
            }

            NameColumn {
                anchors.fill: parent
                anchors.margins: 4
                title: "Version"
                loadedFooter: true
                listColor: XsStyleSheet.panelBgColor
                names: version.names
                selected: version.value
                placeholder: listPlaceholder(version.names.length)
            }
        }
    }
}
