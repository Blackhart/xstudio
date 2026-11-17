// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

import xStudio 1.0
import xstudio.qml.models 1.0

Item {
    id: panel
    anchors.fill: parent

    readonly property int rowHeight: XsStyleSheet.widgetStdHeight
    readonly property int tabHeight: XsStyleSheet.widgetStdHeight + 4
    readonly property int buttonSpacing: 1
    readonly property int panelPadding: XsStyleSheet.panelPadding
    readonly property real leftRatio: 0.3
    readonly property int splitMin: 80
    readonly property int splitFreeMax: 100000

    property string browserMode: "shot"
    property string resultView: "versions"
    property string treeQuery: ""
    property real leftPaneWidth: 0

    function restoreMode() {
        Qt.callLater(function() {
            const saved = modePref.value
            if (saved === "shot" || saved === "asset")
                browserMode = saved
            browserModeAttr.value = browserMode
        })
    }

    function restoreResult() {
        Qt.callLater(function() {
            const saved = resultPref.value
            if (saved === "versions" || saved === "playlist")
                resultView = saved
            resultViewAttr.value = resultView
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

    function chooseResult(mode) {
        resultView = mode
        resultPref.value = mode
        resultViewAttr.value = mode
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

    XsModuleData {
        id: shotbrowserAttributes
        modelDataName: "mikros_shotbrowser_attributes"
    }

    XsAttributeValue {
        id: browserModeAttr
        attributeTitle: "BrowserMode"
        model: shotbrowserAttributes
    }

    XsAttributeValue {
        id: resultViewAttr
        attributeTitle: "ResultView"
        model: shotbrowserAttributes
    }

    XsPreference {
        id: modePref
        path: "/plugin/data_source/mikros_shotbrowser/browser_mode"
        onValueChanged: panel.restoreMode()
    }

    XsPreference {
        id: resultPref
        path: "/plugin/data_source/mikros_shotbrowser/result_view"
        onValueChanged: panel.restoreResult()
    }

    XsPreference {
        id: widthPref
        path: "/plugin/data_source/mikros_shotbrowser/left_pane_width"
        onValueChanged: panel.restoreWidth()
    }

    OpenMediaMenu {
        id: openMenu
    }

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
                            browserModeAttr.value = "shot"
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
                            browserModeAttr.value = "asset"
                        }
                    }

                    Item {
                        Layout.preferredWidth: XsStyleSheet.panelPadding * 4
                        Layout.fillHeight: true
                    }

                    ProjectSelector {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
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

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ShotTree {
                        anchors.fill: parent
                        visible: panel.browserMode === "shot"
                        query: panel.treeQuery
                    }

                    AssetTab {
                        anchors.fill: parent
                        visible: panel.browserMode === "asset"
                        query: panel.treeQuery
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

            VersionList {
                anchors.fill: parent
                anchors.margins: 4
                visible: panel.resultView === "versions"
                resultMode: panel.resultView
                switcherHeight: panel.tabHeight
                switcherSpacing: panel.buttonSpacing * 2
                onResultChosen: function(mode) { panel.chooseResult(mode) }
                onOpenRequested: function(row) { openMenu.openInCurrent(row) }
                onMenuRequested: function(row, item, x, y) { openMenu.showFor(row, item, x, y) }
            }

            PlaylistTree {
                anchors.fill: parent
                anchors.margins: 4
                visible: panel.resultView === "playlist"
                resultMode: panel.resultView
                switcherHeight: panel.tabHeight
                switcherSpacing: panel.buttonSpacing * 2
                onResultChosen: function(mode) { panel.chooseResult(mode) }
                onOpenRequested: function(row) { openMenu.openInCurrent(row) }
                onMenuRequested: function(row, item, x, y) { openMenu.showFor(row, item, x, y) }
            }
        }
    }
}
