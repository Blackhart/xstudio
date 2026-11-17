// SPDX-License-Identifier: Apache-2.0
import QtQuick

import xStudio 1.0

// A version row inside a playlist. The image is the one already stored on the row.
VersionCard {
    id: card

    indent: {
        const depth = modelData !== undefined && modelData !== null && modelData.depth !== undefined
                ? modelData.depth : 0
        return depth * XsStyleSheet.widgetStdHeight / 1.2
    }
    chosen: false
}
