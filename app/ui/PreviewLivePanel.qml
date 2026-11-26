import QtQuick
import QtQuick.Layouts

// Right column: Preview (what Space sends next) and Live (what is on the
// Slide layer), transport buttons, the layer stack, and transition and
// message controls.
Rectangle {
    id: root

    required property QtObject controller
    property string transition: qsTr("Cut")

    // Preview, Live and the Previous / Next buttons always fit: the two
    // frames shrink to the room left (about 150 px of labels, gaps and
    // buttons); the layers and the rest scroll below them.
    readonly property real frameWidth: Math.max(160, Math.min(flick.width, (root.height - 32 - 150) / 2 * 16 / 9))

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
                }
                Row {
                    width: parent.width
                    spacing: 8

                    LButton {
                        objectName: "previousSlide"
                        width: (parent.width - parent.spacing) / 3
                        implicitHeight: 44
                        fontSize: 13
                        text: qsTr("Previous")
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
                        Layout.fillWidth: true
                        implicitHeight: 36
                        radius: 6
                        color: Theme.raised
                        border.color: Theme.border
                        Accessible.role: Accessible.ComboBox
                        Accessible.name: qsTr("Transition: %1").arg(root.transition)

                        LText {
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.transition
                        }
                        LText {
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: "▾"
                            color: Theme.textMuted
                        }
                    }
                }
                LButton {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignBottom
                    radius: 6
                    text: qsTr("Show message…")
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
