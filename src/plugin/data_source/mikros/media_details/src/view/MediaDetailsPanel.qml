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

    XsAttributeValue {
        id: shotgridEnvUrlAttr
        attributeTitle: "ShotgridEnvUrl"
        model: attrs
    }
    property alias shotgridEnvUrl: shotgridEnvUrlAttr.value

    XsAttributeValue {
        id: openShotgridUrl
        attributeTitle: "OpenShotgridUrl"
        model: attrs
    }

    function notifyShotgridLink(url) {
        // Clear first so a second click on the same link still notifies C++.
        openShotgridUrl.value = ""
        openShotgridUrl.value = url
    }

    XsAttributeValue {
        id: statusEditAttr
        attributeTitle: "StatusEdit"
        model: attrs
    }
    XsAttributeValue {
        id: statusEditErrorAttr
        attributeTitle: "StatusEditError"
        model: attrs
    }
    property alias statusEditError: statusEditErrorAttr.value

    XsAttributeValue {
        id: statusEditBusyAttr
        attributeTitle: "StatusEditBusy"
        model: attrs
    }
    property alias statusEditBusy: statusEditBusyAttr.value

    function notifyStatusEdit(entity, id, field, code) {
        statusEditAttr.value = ({})
        statusEditAttr.value = { "entity": entity, "id": id, "field": field, "code": code }
    }

    property int pageIndex: 0
    readonly property bool showTabs: detailState === "loading" || detailState === "ready"

    XsModuleData {
        id: noteAttrs
        modelDataName: "mikros_version_notes_attributes"
    }

    XsAttributeValue {
        id: notesStateAttr
        attributeTitle: "NotesState"
        model: noteAttrs
    }
    property alias notesState: notesStateAttr.value

    XsAttributeValue {
        id: notesErrorAttr
        attributeTitle: "NotesError"
        model: noteAttrs
    }
    property alias notesError: notesErrorAttr.value

    XsAttributeValue {
        id: notesJsonAttr
        attributeTitle: "NotesJson"
        model: noteAttrs
    }
    property alias notesJson: notesJsonAttr.value

    XsAttributeValue {
        id: statusColorsAttr
        attributeTitle: "StatusColors"
        model: noteAttrs
    }
    property alias statusColors: statusColorsAttr.value

    XsAttributeValue {
        id: statusLabelsAttr
        attributeTitle: "StatusLabels"
        model: noteAttrs
    }
    property alias statusLabels: statusLabelsAttr.value

    XsAttributeValue {
        id: statusDefaultColorAttr
        attributeTitle: "StatusDefaultColor"
        model: noteAttrs
    }
    property alias statusDefaultColor: statusDefaultColorAttr.value

    XsAttributeValue {
        id: statusStylesAttr
        attributeTitle: "StatusStyles"
        model: attrs
    }
    property alias statusStyles: statusStylesAttr.value

    XsAttributeValue {
        id: noteStatusEditableAttr
        attributeTitle: "NoteStatusEditable"
        model: noteAttrs
    }
    property alias noteStatusEditable: noteStatusEditableAttr.value

    XsAttributeValue {
        id: noteStatusEdit
        attributeTitle: "NoteStatusEdit"
        model: noteAttrs
    }
    XsAttributeValue {
        id: noteStatusEditError
        attributeTitle: "NoteStatusEditError"
        model: noteAttrs
    }
    property alias noteStatusError: noteStatusEditError.value

    XsAttributeValue {
        id: noteStatusEditBusy
        attributeTitle: "NoteStatusEditBusy"
        model: noteAttrs
    }
    property alias noteStatusBusy: noteStatusEditBusy.value

    function notifyNoteStatusEdit(id, field, code) {
        noteStatusEdit.value = ({})
        noteStatusEdit.value = { "entity": "note", "id": id, "field": field, "code": code }
    }

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
            shotgridBaseUrl: panel.shotgridBaseUrl || panel.shotgridEnvUrl || ""
            onShotgridLink: panel.notifyShotgridLink
            showTabs: panel.showTabs
            pageIndex: panel.pageIndex
            onPageSelected: function(index) { panel.pageIndex = index }
            notesJson: panel.notesJson && typeof panel.notesJson === "object" ? panel.notesJson : ({})
            notesState: panel.notesState || "empty"
            notesError: panel.notesError || ""
            statusColors: panel.statusColors && typeof panel.statusColors === "object" ? panel.statusColors : ({})
            statusLabels: panel.statusLabels && typeof panel.statusLabels === "object" ? panel.statusLabels : ({})
            statusDefaultColor: panel.statusDefaultColor || ""
            statusStyles: panel.statusStyles && typeof panel.statusStyles === "object" ? panel.statusStyles : ({})
            onStatusEdit: panel.notifyStatusEdit
            statusEditBusy: panel.statusEditBusy === true
            statusEditError: panel.statusEditError || ""
            noteStatusEditable: panel.noteStatusEditable === true
            noteStatusBusy: panel.noteStatusBusy === true
            noteStatusError: panel.noteStatusError || ""
            onNoteStatusEdit: panel.notifyNoteStatusEdit
        }
    }
}
