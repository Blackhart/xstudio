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
                visible: projectNames.value && projectNames.value.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                placeholderText: "No project selected"
                model: withNone(projectNames.value)
                currentIndex: indexForSelection(projectNames.value, currentProject.value)
                onActivated: function(index) {
                    currentProject.value = valueFromIndex(projectNames.value, index)
                }
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
                visible: !currentProject.value || (sequenceNames.value && sequenceNames.value.length > 0)
                enabled: !!currentProject.value
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                placeholderText: currentProject.value ? "No sequence selected" : "No project selected"
                model: withNone(sequenceNames.value)
                currentIndex: indexForSelection(sequenceNames.value, currentSequence.value)
                onActivated: function(index) {
                    currentSequence.value = valueFromIndex(sequenceNames.value, index)
                }
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
                visible: currentSequence.value && (!shotNames.value || shotNames.value.length === 0)
                text: "Loading…"
                Layout.fillWidth: true
            }

            XsComboBox {
                visible: !currentSequence.value || (shotNames.value && shotNames.value.length > 0)
                enabled: !!currentSequence.value
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                placeholderText: currentSequence.value ? "No shot selected" : "No sequence selected"
                model: withNone(shotNames.value)
                currentIndex: indexForSelection(shotNames.value, currentShot.value)
                onActivated: function(index) {
                    currentShot.value = valueFromIndex(shotNames.value, index)
                }
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
                visible: !currentProject.value || (assetNames.value && assetNames.value.length > 0)
                enabled: !!currentProject.value
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                placeholderText: currentProject.value ? "No asset selected" : "No project selected"
                model: withNone(assetNames.value)
                currentIndex: indexForSelection(assetNames.value, currentAsset.value)
                onActivated: function(index) {
                    currentAsset.value = valueFromIndex(assetNames.value, index)
                }
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
                visible: !currentProject.value || (playlistNames.value && playlistNames.value.length > 0)
                enabled: !!currentProject.value
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                placeholderText: currentProject.value ? "No playlist selected" : "No project selected"
                model: withNone(playlistNames.value)
                currentIndex: indexForSelection(playlistNames.value, currentPlaylist.value)
                onActivated: function(index) {
                    currentPlaylist.value = valueFromIndex(playlistNames.value, index)
                }
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
                visible: !currentProject.value || (versionNames.value && versionNames.value.length > 0)
                enabled: !!currentProject.value
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                placeholderText: currentProject.value ? "No version selected" : "No project selected"
                model: withNone(versionNames.value)
                currentIndex: indexForSelection(versionNames.value, currentVersion.value)
                onActivated: function(index) {
                    currentVersion.value = valueFromIndex(versionNames.value, index)
                }
            }
        }
    }
}
