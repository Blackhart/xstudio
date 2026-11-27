// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts

import xStudio 1.0

// Colored capsule for a ShotGrid-style status code.
Rectangle {
    id: badge

    property string code: ""
    property string label: code === "" ? "N/A" : code
    property string tone: "" ///< Schema fill. Empty keeps the built-in status map.
    property string accessibleLabel: label
    property bool showsArrow: false ///< Draws a down chevron inside the capsule.

    readonly property color fill: {
        if (tone !== "")
            return tone
        const key = code.toLowerCase()
        if (key === "fin" || key === "final" || key === "apr" || key === "approved")
            return "#2e7d32"
        if (key === "rev" || key === "retake" || key === "rtk")
            return "#c62828"
        if (key === "ip" || key === "wip" || key === "inprogress")
            return "#ef6c00"
        if (key === "rdy" || key === "ready" || key === "revw")
            return "#1565c0"
        if (code === "")
            return XsStyleSheet.widgetBgNormalColor
        return "#546e7a"
    }

    readonly property color ink: code === "" ? XsStyleSheet.secondaryTextColor : "white"

    Accessible.name: accessibleLabel
    Accessible.role: showsArrow ? Accessible.Button : Accessible.StaticText
    visible: true
    implicitHeight: XsStyleSheet.widgetStdHeight - 4
    implicitWidth: Math.max(labelText.implicitWidth + (showsArrow ? 18 : 0) + 16, 40)
    radius: height / 2
    color: fill

    Text {
        id: labelText
        anchors.verticalCenter: parent.verticalCenter
        x: badge.showsArrow ? 8 : (badge.width - width) / 2
        text: badge.label
        color: badge.ink
        font.pixelSize: XsStyleSheet.fontSize
        font.family: XsStyleSheet.fontFamily
    }

    Text {
        visible: badge.showsArrow
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 8
        text: "▾"
        color: badge.ink
        font.pixelSize: XsStyleSheet.fontSize
        font.family: XsStyleSheet.fontFamily
    }
}
