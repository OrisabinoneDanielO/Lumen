import QtQuick
import QtQuick.Layouts

// Right column: Preview (what Space sends next) and Live (what is on the
// Slide layer), transport buttons, the layer stack, and transition and
// message controls.
Rectangle {
    id: root

    required property QtObject controller
    readonly property string transitionName: {
        const list = root.controller.transitions
        for (let i = 0; i < list.length; ++i)
            if (list[i].key === root.controller.transition)
                return list[i].name
        return qsTr("Cut")
    }

    // Preview, Live, the Previous / Next buttons and the Transition row always
    // fit: the two frames shrink to the room left (about 210 px of labels,
    // gaps, buttons and the transition row); the layers scroll below them.
    readonly property real frameWidth: Math.max(160, Math.min(flick.width, (root.height - 32 - 210) / 2 * 16 / 9))
    signal messageRequested()

    color: Theme.panel

    Rectangle {
        width: 1
        height: parent.height
        color: Theme.border
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: 16
        anchors.leftMargin: 17
        clip: true
        contentHeight: column.height
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column
            width: parent.width
            spacing: 14

            Column {
                width: parent.width
                spacing: 6

                SectionLabel {
                    text: qsTr("PREVIEW (NEXT)")
                    color: Theme.link
                }
                SlideFrame {
                    objectName: "previewFrame"
                    width: root.frameWidth
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.controller.previewText
                    slideStyle: root.controller.previewStyle
                    footer: root.controller.previewFooter
                    poster: root.controller.previewPoster
                    timerText: root.controller.previewTimerText
                    elements: root.controller.previewElements
                    background: root.controller.previewBackground
                    frameColor: Theme.next
                }
            }

            Column {
                width: parent.width
                spacing: 6

                Item {
                    width: parent.width
                    height: liveTag.height

                    SectionLabel {
                        id: liveTag
                        text: qsTr("LIVE")
                        font.bold: true
                        color: Theme.live
                    }
                    LText {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.controller.liveLabel
                        font.pixelSize: 12
                        color: Theme.textMuted
                    }
                }
                SlideFrame {
                    objectName: "liveFrame"
                    width: root.frameWidth
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.controller.liveText
                    slideStyle: root.controller.liveStyle
                    footer: root.controller.liveFooter
                    poster: root.controller.livePoster
                    timerText: root.controller.liveTimerText
                    elements: root.controller.liveElements
                    background: root.controller.liveBackground
                    frameColor: Theme.live
                    frameWidth: 3
                    fontSize: 15
                    bold: true

                    // What the audience really sees over the slide: Black,
                    // the Logo, or nothing at all when the output is off.
                    Rectangle {
                        objectName: "liveOverride"
                        anchors.fill: parent
                        anchors.margins: 3
                        visible: root.controller.blackOn || root.controller.logoOn
                        color: "#000000"
                        LText {
                            anchors.centerIn: parent
                            text: root.controller.blackOn ? qsTr("Black (F7)") : qsTr("Logo (F6)")
                            color: Theme.textMuted
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                    }
                    Rectangle {
                        objectName: "liveOutputOff"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 3
                        height: 24
                        visible: !root.controller.outputConnected
                        color: Theme.warnBackground
                        LText {
                            anchors.centerIn: parent
                            text: qsTr("Output off: the audience sees nothing")
                            color: Theme.warnText
                            font.pixelSize: 11
                        }
                    }
                }
                Row {
                    width: parent.width
                    spacing: 8

                    LButton {
                        objectName: "previousSlide"
                        width: (parent.width - parent.spacing) / 3
                        implicitHeight: 44
                        fontSize: 13
                        text: qsTr("Previous (←)")
                        onClicked: root.controller.previous()
                    }
                    LButton {
                        objectName: "nextSlide"
                        width: (parent.width - parent.spacing) * 2 / 3
                        implicitHeight: 44
                        fontSize: 13
                        variant: "primary"
                        text: qsTr("Next slide (Space)")
                        onClicked: root.controller.next()
                    }
                }
            }
            RowLayout {
                width: parent.width
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 4

                    LText {
                        text: qsTr("Transition")
                        font.pixelSize: 12
                        color: Theme.textMuted
                    }
                    Rectangle {
                        objectName: "transitionBox"
                        Layout.fillWidth: true
                        implicitHeight: 36
                        radius: 6
                        color: transitionArea.containsMouse ? Theme.selected : Theme.raised
                        border.color: transitionMenu.visible ? Theme.live : Theme.border
                        Accessible.role: Accessible.ComboBox
                        Accessible.name: qsTr("Transition: %1").arg(root.transitionName)

                        LText {
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.right: arrow.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.controller.transition === "cut" ? root.transitionName
                                  : qsTr("%1 · %2 s").arg(root.transitionName).arg((root.controller.transitionMs / 1000).toFixed(1))
                        }
                        LText {
                            id: arrow
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: "▾"
                            color: Theme.textMuted
                        }
                        MouseArea {
                            id: transitionArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: transitionMenu.visible = !transitionMenu.visible
                        }
                    }
                }
                LButton {
                    objectName: "showMessageButton"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignBottom
                    radius: 6
                    text: qsTr("Show message…")
                    onClicked: root.messageRequested()
                }
            }

            Column {
                width: parent.width
                spacing: 4

                SectionLabel {
                    text: qsTr("LAYERS")
                    bottomPadding: 4
                }
                Repeater {
                    model: root.controller.layers
                    delegate: Rectangle {
                        id: layerRow
                        required property var modelData
                        width: parent ? parent.width : 0
                        height: 34
                        radius: 6
                        color: Theme.raised
                        Accessible.role: Accessible.StaticText
                        Accessible.name: modelData.name + ": " + modelData.value

                        LText {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: layerRow.modelData.name
                        }
                        LText {
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, parent.width * 0.6)
                            horizontalAlignment: Text.AlignRight
                            text: layerRow.modelData.value
                            color: layerRow.modelData.active ? Theme.text : Theme.textMuted
                        }
                    }
                }
            }

        }
    }

    // The transitions, and how long they take (opens from the Transition box).
    Rectangle {
        id: transitionMenu
        objectName: "transitionMenu"
        visible: false
        z: 20
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        height: menuColumn.implicitHeight + 16
        radius: 8
        color: Theme.window
        border.color: Theme.border

        Column {
            id: menuColumn
            anchors.fill: parent
            anchors.margins: 8
            spacing: 2

            Repeater {
                model: root.controller.transitions
                delegate: Rectangle {
                    required property var modelData
                    objectName: "transition-" + modelData.key
                    width: menuColumn.width
                    height: 30
                    radius: 6
                    color: modelData.key === root.controller.transition ? Theme.selected
                           : optionArea.containsMouse ? Theme.raised : "transparent"
                    LText {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.modelData.name
                        font.weight: parent.modelData.key === root.controller.transition ? Font.DemiBold : Font.Normal
                    }
                    MouseArea {
                        id: optionArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.controller.transition = parent.modelData.key
                            if (parent.modelData.key === "cut")
                                transitionMenu.visible = false
                        }
                    }
                }
            }
            Row {
                visible: root.controller.transition !== "cut"
                spacing: 4
                topPadding: 6
                Repeater {
                    model: [{ ms: 300, name: qsTr("Fast") }, { ms: 600, name: qsTr("Normal") }, { ms: 1000, name: qsTr("Slow") }]
                    delegate: LButton {
                        required property var modelData
                        objectName: "transitionSpeed-" + modelData.ms
                        text: modelData.name
                        implicitHeight: 28
                        radius: 6
                        fontSize: 11
                        variant: root.controller.transitionMs === modelData.ms ? "primary" : "default"
                        onClicked: {
                            root.controller.transitionMs = modelData.ms
                            transitionMenu.visible = false
                        }
                    }
                }
            }
        }
    }

    // When the column is taller than the window: a thin bar shows it scrolls.
    Rectangle {
        objectName: "previewLiveScroll"
        visible: flick.contentHeight > flick.height + 1
        anchors.right: parent.right
        anchors.rightMargin: 3
        y: flick.y + flick.visibleArea.yPosition * flick.height
        width: 4
        height: Math.max(24, flick.visibleArea.heightRatio * flick.height)
        radius: 2
        color: Theme.border
    }
}
