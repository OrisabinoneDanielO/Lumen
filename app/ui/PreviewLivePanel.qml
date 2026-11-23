import QtQuick
import QtQuick.Layouts

// Right column: Preview (next) and Live frames, transport, layer stack,
// transition and message controls.
Rectangle {
    id: root

    property string nextText
    property string liveText
    property string liveLabel
    // Top to bottom, as in the mockup. Each: { name, value, active }
    property var layers: []
    property string transition: qsTr("Fade, 0.5 s")

    color: Theme.panel

    Rectangle {
        width: 1
        height: parent.height
        color: Theme.border
    }

    Flickable {
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
                    width: parent.width
                    text: root.nextText
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
                        text: root.liveLabel
                        font.pixelSize: 12
                        color: Theme.textMuted
                    }
                }
                SlideFrame {
                    objectName: "liveFrame"
                    width: parent.width
                    text: root.liveText
                    frameColor: Theme.live
                    frameWidth: 3
                    fontSize: 15
                    bold: true
                }
                Row {
                    width: parent.width
                    spacing: 8

                    LButton {
                        width: (parent.width - parent.spacing) / 3
                        implicitHeight: 44
                        fontSize: 13
                        text: qsTr("Previous")
                    }
                    LButton {
                        width: (parent.width - parent.spacing) * 2 / 3
                        implicitHeight: 44
                        fontSize: 13
                        variant: "primary"
                        text: qsTr("Next slide (Space)")
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
                    model: root.layers
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
}
