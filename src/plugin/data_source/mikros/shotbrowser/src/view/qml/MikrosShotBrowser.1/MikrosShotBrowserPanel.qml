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
    readonly property url assetIcon: "qrc:/mikros_shotbrowser_icons/nature.svg"
    readonly property url versionIcon: "qrc:/mikros_shotbrowser_icons/segment.svg"

    property string browserMode: "shot"

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

    function listPlaceholder() {
        return project.value ? "Loading…" : "No project selected"
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

    component NameColumn: ColumnLayout {
        id: column
        property string title
        property url iconSrc
        property var names
        property string selected
        property string placeholder

        readonly property int count: names && names.length ? names.length : 0

        spacing: XsStyleSheet.panelPadding

        Bar {
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
                text: column.count
                opacity: 0.5
                font.pixelSize: XsStyleSheet.fontSize * 1.2
                horizontalAlignment: Text.AlignRight
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 2
            color: XsStyleSheet.panelBgColor
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

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
                spacing: 0
                model: column.names

                ScrollBar.vertical: XsScrollBar {
                    visible: nameView.height < nameView.contentHeight
                    policy: ScrollBar.AsNeeded
                }

                delegate: Rectangle {
                    id: row
                    required property string modelData

                    width: nameView.width
                    height: XsStyleSheet.widgetStdHeight
                    color: modelData === column.selected
                           ? Qt.darker(XsStyleSheet.accentColor, 2)
                           : rowHover.hovered
                             ? XsStyleSheet.widgetBgNormalColor
                             : "transparent"
                    border.width: 1
                    border.color: rowHover.hovered ? XsStyleSheet.accentColor : "transparent"

                    XsText {
                        anchors.fill: parent
                        leftPadding: 2
                        rightPadding: 8
                        text: modelData
                        font.pixelSize: XsStyleSheet.fontSize * 1.2
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }

                    HoverHandler {
                        id: rowHover
                    }
                }
            }
        }
    }

    XsGradientRectangle {
        anchors.fill: parent
    }

    XsModuleData {
        id: shotbrowserAttributes
        modelDataName: "mikros_shotbrowser_attributes"
    }

    Catalog { id: project; title: "Project"; source: shotbrowserAttributes }
    Catalog { id: sequence; title: "Sequence"; source: shotbrowserAttributes }
    Catalog { id: shot; title: "Shot"; source: shotbrowserAttributes }
    Catalog { id: asset; title: "Asset"; source: shotbrowserAttributes }
    Catalog { id: version; title: "Version"; source: shotbrowserAttributes }

    XsSplitView {
        id: mainSplit
        anchors.fill: parent
        anchors.margins: panel.panelPadding

        // Until the handle is pressed, the left pane is clamped to leftRatio.
        // SplitView otherwise keeps the column's implicit width.
        property bool dragStarted: false
        property real draggedWidth: 0
        readonly property real startupWidth: Math.max(1, width * panel.leftRatio)

        handle: Rectangle {
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
                if (handlePressed) {
                    mainSplit.dragStarted = true
                    mainSplit.draggedWidth = leftPane.width
                }
            }
        }

        Item {
            id: leftPane
            SplitView.fillHeight: true
            SplitView.minimumWidth: panel.splitMin
            SplitView.preferredWidth: mainSplit.dragStarted ? mainSplit.draggedWidth : mainSplit.startupWidth
            SplitView.maximumWidth: mainSplit.dragStarted ? panel.splitFreeMax : mainSplit.startupWidth
            onWidthChanged: {
                if (mainSplit.dragStarted)
                    mainSplit.draggedWidth = width
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: panel.panelPadding

                Bar {
                    barHeight: panel.tabHeight
                    spacing: panel.buttonSpacing

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
                        Layout.preferredWidth: panel.tabHeight
                        Layout.fillHeight: true
                        imgSrc: "qrc:/icons/filter.svg"
                        toolTip: "Status"
                        isActive: false
                    }
                }

                Bar {
                    barHeight: panel.tabHeight
                    spacing: panel.buttonSpacing

                    Repeater {
                        model: [
                            { label: "Shot", mode: "shot" },
                            { label: "Asset", mode: "asset" }
                        ]

                        XsPrimaryButton {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            font.weight: Font.Medium
                            text: modelData.label
                            toolTip: modelData.label
                            isActive: panel.browserMode === modelData.mode
                            onClicked: panel.browserMode = modelData.mode
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    XsSplitView {
                        id: treeSplit
                        anchors.fill: parent
                        visible: panel.browserMode === "shot"

                        property real sequencePaneWidth: -1

                        NameColumn {
                            id: sequenceColumn
                            SplitView.minimumWidth: panel.splitMin
                            SplitView.fillHeight: true
                            SplitView.preferredWidth: treeSplit.sequencePaneWidth < 0
                                                      ? Math.max(panel.splitMin, treeSplit.width * 0.5)
                                                      : treeSplit.sequencePaneWidth
                            onWidthChanged: {
                                if (SplitView.view && SplitView.view.resizing)
                                    treeSplit.sequencePaneWidth = width
                            }
                            title: "Sequence"
                            names: sequence.names
                            selected: sequence.value
                            placeholder: listPlaceholder()
                        }

                        NameColumn {
                            SplitView.fillWidth: true
                            SplitView.minimumWidth: panel.splitMin
                            SplitView.fillHeight: true
                            title: "Shot"
                            names: shot.names
                            selected: shot.value
                            placeholder: listPlaceholder()
                        }
                    }

                    NameColumn {
                        anchors.fill: parent
                        visible: panel.browserMode === "asset"
                        title: "Asset"
                        iconSrc: panel.assetIcon
                        names: asset.names
                        selected: asset.value
                        placeholder: listPlaceholder()
                    }
                }
            }
        }

        NameColumn {
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            SplitView.minimumWidth: 160
            title: "Version"
            iconSrc: panel.versionIcon
            names: version.names
            selected: version.value
            placeholder: listPlaceholder()
        }
    }
}
