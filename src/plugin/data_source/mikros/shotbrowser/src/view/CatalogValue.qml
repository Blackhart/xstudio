// SPDX-License-Identifier: Apache-2.0
import QtQuick

import xStudio 1.0

// One catalog attribute: the current label and the choice list.
Item {
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
