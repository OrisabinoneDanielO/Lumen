import QtQuick
import QtQuick.Layouts

// Centre and style panel in Edit mode, after design/mockups/Editor.html.
// Read-only for now: the canvas shows the selected slide and the controls are
// disabled until themes (batch 8) and the full editor (batch 16). Nothing in
// Edit mode can reach the screen. Shown only with experimental/editor on.
Rectangle {
    id: root

    required property QtObject controller
    readonly property var selected: controller.slides.count > 0
                                    ? controller.slides.get(controller.selectedSlide) : null

    color: Theme.window

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                LText {
                    text: qsTr("Insert")
                    font.pixelSize: 12
                    color: Theme.textMuted
                }
                Repeater {
                    model: [qsTr("Text box"), qsTr("Shape"), qsTr("Image"), qsTr("Video"),
                            qsTr("Clock"), qsTr("Countdown")]
                    delegate: LButton {
                        required property string modelData
                        text: modelData
                        implicitHeight: 32
                        opacity: 0.5
                        enabled: false
                    }
                }
                Item { Layout.fillWidth: true }
                LText {
                    text: "1920 × 1080"
                    font.pixelSize: 12
                    color: Theme.textMuted
                }
            }

            // Canvas: the selected slide, with the title-safe area.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Rectangle {
                    id: canvas
                    objectName: "editorCanvas"
                    anchors.centerIn: parent
                    width: Math.min(parent.width, parent.height * 16 / 9)
                    height: width * 9 / 16
                    color: Theme.slideBackground
                    border.color: Theme.border

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: parent.width * 0.05
                        color: "transparent"
                        border.color: "#40FFFFFF"
                        border.width: 1
                    }
                    LText {
                        anchors.fill: parent
                        anchors.margins: parent.width * 0.08
                        text: root.selected ? root.selected.text : ""
                        color: "#FFFFFF"
                        font.pixelSize: Math.max(12, canvas.height * 0.07)
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // Slide strip: click to choose which slide to edit.
            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: 104
                clip: true
                contentWidth: strip.width
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds

                Row {
                    id: strip
                    spacing: 8

                    Repeater {
                        model: root.controller.slides
                        delegate: Rectangle {
                            id: card
                            required property int index
                            required property string group
                            required property string text

                            objectName: "editorSlide-" + index
                            width: 150
                            height: 100
                            radius: 8
                            color: Theme.panel
                            border.width: 2
                            border.color: index === root.controller.selectedSlide ? Theme.next : Theme.border
                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr("Slide %1, %2").arg(index + 1).arg(group)

                            LText {
                                x: 8
                                y: 6
                                text: card.group + " · " + (card.index + 1)
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                            Rectangle {
                                x: 6
                                y: 26
                                width: parent.width - 12
                                height: parent.height - 32
                                radius: 4
                                color: Theme.slideBackground
                                LText {
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    text: card.text
                                    color: "#FFFFFF"
                                    font.pixelSize: 8
                                    wrapMode: Text.WordWrap
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.controller.activateSlide(card.index)
                            }
                        }
                    }
                }
            }
        }

        // Style panel (disabled until themes arrive).
        Rectangle {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            color: Theme.panel

            Rectangle {
                width: 1
                height: parent.height
                color: Theme.border
            }

            Column {
                x: 17
                y: 16
                width: parent.width - 33
                spacing: 16
                opacity: 0.55

                SectionLabel { text: qsTr("THEME") }
                LText {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Themes and text styling arrive in a later version. Edit mode never changes the live output.")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                SectionLabel { text: qsTr("TEXT") }
                Row {
                    spacing: 6
                    Repeater {
                        model: [qsTr("Left"), qsTr("Centre"), qsTr("Right")]
                        delegate: LButton {
                            required property string modelData
                            text: modelData
                            enabled: false
                        }
                    }
                }
                SectionLabel { text: qsTr("COLOUR") }
                Row {
                    spacing: 8
                    Repeater {
                        model: ["#FFFFFF", "#FFE7A3", "#BFD3FF", "#000000"]
                        delegate: Rectangle {
                            required property string modelData
                            width: 28
                            height: 28
                            radius: 14
                            color: modelData
                            border.color: Theme.border
                        }
                    }
                }
            }
        }
    }
}
