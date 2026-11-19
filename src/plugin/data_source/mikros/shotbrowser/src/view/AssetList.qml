// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts

import xStudio 1.0
import xstudio.qml.models 1.0

// The asset list, filtered by the same search as the shot tree.
Item {
    id: assets

    property string query: ""

    function listPlaceholder() {
        if (!project.value || project.value === "-1")
            return "No project selected"
        if (!assetsLoaded.value)
            return "Loading…"
        return "No results"
    }

    function filteredNames(names) {
        if (!names || !names.length)
            return []
        if (!query)
            return names
        const text = query.toLowerCase()
        const matched = []
        for (let i = 0; i < names.length; i++) {
            if (String(names[i]).toLowerCase().indexOf(text) !== -1)
                matched.push(names[i])
        }
        return matched
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

    function chooseAsset(name) {
        asset.value = asset.value === name ? "" : name
    }

    XsModuleData {
        id: attributes
        modelDataName: "mikros_shotbrowser_attributes"
    }

    CatalogValue { id: project; title: "SelectedProject"; source: attributes }
    CatalogValue { id: asset; title: "Asset"; source: attributes }
    CatalogValue { id: footer; title: "AssetListFooter"; source: attributes }

    XsAttributeValue {
        id: assetsLoaded
        attributeTitle: "AssetsLoaded"
        model: attributes
    }

    NameColumn {
        anchors.fill: parent
        title: ""
        tree: true
        listColor: XsStyleSheet.panelBgColor
        names: assets.assetTreeRows()
        placeholder: {
            project.value
            assetsLoaded.value
            return assets.listPlaceholder()
        }
        loadedFooter: true
        customFooter: true
        footerText: footer.value
        rowDelegate: assetRow
    }

    Component {
        id: assetRow

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
            readonly property bool chosen: label === asset.value

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
                        onTapped: assets.chooseAsset(line.label)
                    }
                }
            }

            HoverHandler {
                id: rowHover
            }
        }
    }
}
