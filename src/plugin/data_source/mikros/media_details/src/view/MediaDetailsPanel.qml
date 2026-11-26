// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts

import xStudio 1.0
import xstudio.qml.models 1.0

Item {
    id: panel
    anchors.fill: parent

    XsModuleData {
        id: attrs
        modelDataName: "mikros_media_details_attributes"
    }

    XsAttributeValue {
        id: detailStateAttr
        attributeTitle: "DetailState"
        model: attrs
    }
    property alias detailState: detailStateAttr.value

    XsAttributeValue {
        id: detailErrorAttr
        attributeTitle: "DetailError"
        model: attrs
    }
    property alias detailError: detailErrorAttr.value

    XsAttributeValue {
        id: detailJsonAttr
        attributeTitle: "DetailJson"
        model: attrs
    }
    property alias detailJson: detailJsonAttr.value

    XsAttributeValue {
        id: shotgridUrlAttr
        attributeTitle: "ShotgridBaseUrl"
        model: attrs
    }
    property alias shotgridBaseUrl: shotgridUrlAttr.value

    Rectangle {
        anchors.fill: parent
        color: XsStyleSheet.panelBgColor
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: XsStyleSheet.panelPadding
        spacing: 4

        Text {
            Layout.fillWidth: true
            text: "Media Details"
            color: XsStyleSheet.primaryTextColor
            font.pixelSize: XsStyleSheet.fontSize * 1.1
            font.family: XsStyleSheet.fontFamily
            font.bold: true
        }

        DetailSections {
            Layout.fillWidth: true
            Layout.fillHeight: true
            detail: detailJson && typeof detailJson === "object" ? detailJson : ({})
            detailState: panel.detailState || "empty"
            detailError: panel.detailError || ""
            shotgridBaseUrl: panel.shotgridBaseUrl || ""
        }
    }
}
