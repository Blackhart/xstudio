// SPDX-License-Identifier: Apache-2.0
import QtQuick
import QtQuick.Layouts

import xStudio 1.0
import xstudio.qml.models 1.0

Item {
    id: panel
    anchors.fill: parent

    // First combo entry: deselect / return to the empty placeholder state.
    readonly property string noneLabel: "None"

    function withNone(options) {
        return [noneLabel].concat(options && options.length ? options : [])
    }

    function indexForSelection(options, selected) {
        if (!selected)
            return 0
        const idx = options ? options.indexOf(selected) : -1
        return idx >= 0 ? idx + 1 : 0
    }

    function valueFromIndex(options, index) {
        if (index <= 0 || !options || index > options.length)
            return ""
        return options[index - 1]
    }

    // ComboBox assigns currentIndex itself and drops a binding. Re-apply the
    // attribute after the model settles so a parent change shows None.
    function syncCombo(combo, options, selected) {
        const next = indexForSelection(options, selected)
        if (combo.currentIndex !== next)
            combo.currentIndex = next
    }

    function deferSync(combo, optionsAttr, valueAttr) {
        Qt.callLater(function() {
            syncCombo(combo, optionsAttr.value, valueAttr.value)
        })
    }

    function loadedCount(options) {
        return options && options.length ? options.length : 0
    }

    XsGradientRectangle {
        anchors.fill: parent
    }

    XsModuleData {
        id: shotbrowserAttributes
        modelDataName: "mikros_shotbrowser_attributes"
    }

    XsAttributeValue {
        id: currentProject
        attributeTitle: "Project"
        model: shotbrowserAttributes
    }

    XsAttributeValue {
        id: projectNames
        attributeTitle: "Project"
        model: shotbrowserAttributes
        role: "combo_box_options"
    }

    XsAttributeValue {
        id: currentSequence
        attributeTitle: "Sequence"
        model: shotbrowserAttributes
    }

    XsAttributeValue {
        id: sequenceNames
        attributeTitle: "Sequence"
        model: shotbrowserAttributes
        role: "combo_box_options"
    }

    XsAttributeValue {
        id: currentShot
        attributeTitle: "Shot"
        model: shotbrowserAttributes
    }

    XsAttributeValue {
        id: shotNames
        attributeTitle: "Shot"
        model: shotbrowserAttributes
        role: "combo_box_options"
    }

    XsAttributeValue {
        id: currentAsset
        attributeTitle: "Asset"
        model: shotbrowserAttributes
    }

    XsAttributeValue {
        id: assetNames
        attributeTitle: "Asset"
        model: shotbrowserAttributes
        role: "combo_box_options"
    }

    XsAttributeValue {
        id: currentPlaylist
        attributeTitle: "Playlist"
        model: shotbrowserAttributes
    }

    XsAttributeValue {
        id: playlistNames
        attributeTitle: "Playlist"
        model: shotbrowserAttributes
        role: "combo_box_options"
    }

    XsAttributeValue {
        id: currentVersion
        attributeTitle: "Version"
        model: shotbrowserAttributes
    }

    XsAttributeValue {
        id: versionNames
        attributeTitle: "Version"
        model: shotbrowserAttributes
        role: "combo_box_options"
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            height: 32

            XsText {
                text: "Project"
                Layout.preferredWidth: 72
            }

            XsText {
                visible: !projectNames.value || projectNames.value.length === 0
                text: "Loading…"
                Layout.fillWidth: true
            }

            XsComboBox {
                id: projectCombo
                visible: projectNames.value && projectNames.value.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                placeholderText: "No project selected"
                model: withNone(projectNames.value)
                Component.onCompleted: panel.deferSync(projectCombo, projectNames, currentProject)
                onModelChanged: panel.deferSync(projectCombo, projectNames, currentProject)
                onActivated: function(index) {
                    currentProject.value = valueFromIndex(projectNames.value, index)
                }
                Connections {
                    target: currentProject
                    function onValueChanged() {
                        panel.deferSync(projectCombo, projectNames, currentProject)
                    }
                }
            }

            XsText {
                text: loadedCount(projectNames.value)
                Layout.preferredWidth: 40
                horizontalAlignment: Text.AlignRight
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            height: 32

            XsText {
                text: "Sequence"
                Layout.preferredWidth: 72
            }

            XsText {
                visible: currentProject.value && (!sequenceNames.value || sequenceNames.value.length === 0)
                text: "Loading…"
                Layout.fillWidth: true
            }

            XsComboBox {
                id: sequenceCombo
                visible: !currentProject.value || (sequenceNames.value && sequenceNames.value.length > 0)
                enabled: !!currentProject.value
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                placeholderText: currentProject.value ? "No sequence selected" : "No project selected"
                model: withNone(sequenceNames.value)
                Component.onCompleted: panel.deferSync(sequenceCombo, sequenceNames, currentSequence)
                onModelChanged: panel.deferSync(sequenceCombo, sequenceNames, currentSequence)
                onActivated: function(index) {
                    currentSequence.value = valueFromIndex(sequenceNames.value, index)
                }
                Connections {
                    target: currentSequence
                    function onValueChanged() {
                        panel.deferSync(sequenceCombo, sequenceNames, currentSequence)
                    }
                }
            }

            XsText {
                text: loadedCount(sequenceNames.value)
                Layout.preferredWidth: 40
                horizontalAlignment: Text.AlignRight
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            height: 32

            XsText {
                text: "Shot"
                Layout.preferredWidth: 72
            }

            XsText {
                visible: currentProject.value && !currentAsset.value && (!shotNames.value || shotNames.value.length === 0)
                text: "Loading…"
                Layout.fillWidth: true
            }

            XsComboBox {
                id: shotCombo
                visible: !currentProject.value || !!currentAsset.value || (shotNames.value && shotNames.value.length > 0)
                enabled: !!currentProject.value && !currentAsset.value
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                placeholderText: currentProject.value ? "No shot selected" : "No project selected"
                model: withNone(shotNames.value)
                Component.onCompleted: panel.deferSync(shotCombo, shotNames, currentShot)
                onModelChanged: panel.deferSync(shotCombo, shotNames, currentShot)
                onActivated: function(index) {
                    currentShot.value = valueFromIndex(shotNames.value, index)
                }
                Connections {
                    target: currentShot
                    function onValueChanged() {
                        panel.deferSync(shotCombo, shotNames, currentShot)
                    }
                }
            }

            XsText {
                text: loadedCount(shotNames.value)
                Layout.preferredWidth: 40
                horizontalAlignment: Text.AlignRight
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            height: 32

            XsText {
                text: "Asset"
                Layout.preferredWidth: 72
            }

            XsText {
                visible: currentProject.value && (!assetNames.value || assetNames.value.length === 0)
                text: "Loading…"
                Layout.fillWidth: true
            }

            XsComboBox {
                id: assetCombo
                visible: !currentProject.value || (assetNames.value && assetNames.value.length > 0)
                enabled: !!currentProject.value
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                placeholderText: currentProject.value ? "No asset selected" : "No project selected"
                model: withNone(assetNames.value)
                Component.onCompleted: panel.deferSync(assetCombo, assetNames, currentAsset)
                onModelChanged: panel.deferSync(assetCombo, assetNames, currentAsset)
                onActivated: function(index) {
                    currentAsset.value = valueFromIndex(assetNames.value, index)
                }
                Connections {
                    target: currentAsset
                    function onValueChanged() {
                        panel.deferSync(assetCombo, assetNames, currentAsset)
                    }
                }
            }

            XsText {
                text: loadedCount(assetNames.value)
                Layout.preferredWidth: 40
                horizontalAlignment: Text.AlignRight
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            height: 32

            XsText {
                text: "Playlist"
                Layout.preferredWidth: 72
            }

            XsText {
                visible: currentProject.value && (!playlistNames.value || playlistNames.value.length === 0)
                text: "Loading…"
                Layout.fillWidth: true
            }

            XsComboBox {
                id: playlistCombo
                visible: !currentProject.value || (playlistNames.value && playlistNames.value.length > 0)
                enabled: !!currentProject.value
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                placeholderText: currentProject.value ? "No playlist selected" : "No project selected"
                model: withNone(playlistNames.value)
                Component.onCompleted: panel.deferSync(playlistCombo, playlistNames, currentPlaylist)
                onModelChanged: panel.deferSync(playlistCombo, playlistNames, currentPlaylist)
                onActivated: function(index) {
                    currentPlaylist.value = valueFromIndex(playlistNames.value, index)
                }
                Connections {
                    target: currentPlaylist
                    function onValueChanged() {
                        panel.deferSync(playlistCombo, playlistNames, currentPlaylist)
                    }
                }
            }

            XsText {
                text: loadedCount(playlistNames.value)
                Layout.preferredWidth: 40
                horizontalAlignment: Text.AlignRight
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            height: 32

            XsText {
                text: "Version"
                Layout.preferredWidth: 72
            }

            XsText {
                visible: currentProject.value && (!versionNames.value || versionNames.value.length === 0)
                text: "Loading…"
                Layout.fillWidth: true
            }

            XsComboBox {
                id: versionCombo
                visible: !currentProject.value || (versionNames.value && versionNames.value.length > 0)
                enabled: !!currentProject.value
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                placeholderText: currentProject.value ? "No version selected" : "No project selected"
                model: withNone(versionNames.value)
                Component.onCompleted: panel.deferSync(versionCombo, versionNames, currentVersion)
                onModelChanged: panel.deferSync(versionCombo, versionNames, currentVersion)
                onActivated: function(index) {
                    currentVersion.value = valueFromIndex(versionNames.value, index)
                }
                Connections {
                    target: currentVersion
                    function onValueChanged() {
                        panel.deferSync(versionCombo, versionNames, currentVersion)
                    }
                }
            }

            XsText {
                text: loadedCount(versionNames.value)
                Layout.preferredWidth: 40
                horizontalAlignment: Text.AlignRight
            }
        }
    }
}
