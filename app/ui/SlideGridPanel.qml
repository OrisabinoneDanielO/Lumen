import QtQuick
import QtQuick.Layouts

// Centre region in Show mode: the selected item's title, arrangement and
// slide cards in play order. Clicking a card sends it live.
Item {
    id: root

    required property QtObject controller
    signal slideClicked(int index)
    signal cuesRequested() // batch 20: the Cues popover
    property bool cuesAvailable: false

    Flickable {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.topMargin: 16
        clip: true
        contentHeight: content.height + 16
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: content
            width: parent.width
            spacing: 14

            RowLayout {
                width: parent.width
                visible: root.controller.itemTitle !== ""
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    LText {
                        Layout.fillWidth: true
                        text: root.controller.itemTitle
                        font.pixelSize: 22
                        font.bold: true
                    }
                    LText {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: root.controller.itemDetail
                        color: Theme.textMuted
                    }
                }

                LButton {
                    objectName: "cuesButton"
                    Layout.alignment: Qt.AlignBottom
                    visible: root.cuesAvailable
                    text: qsTr("Cues")
                    implicitHeight: 28
                    radius: 6
                    fontSize: 11
                    onClicked: root.cuesRequested()
                }

                Row {
                    Layout.alignment: Qt.AlignBottom
                    spacing: 6
                    visible: root.controller.arrangement.length > 0

                    LText {
                        anchors.verticalCenter: parent.verticalCenter
                        rightPadding: 4
                        text: qsTr("Arrangement")
                        font.pixelSize: 12
                        color: Theme.textMuted
                    }
                    Repeater {
                        model: root.controller.arrangement
                        delegate: Rectangle {
                            id: chip
                            required property var modelData
                            width: chipLabel.implicitWidth + 20
                            height: 24
                            radius: 6
                            color: modelData.background
                            LText {
                                id: chipLabel
                                anchors.centerIn: parent
                                text: chip.modelData.label
                                color: chip.modelData.foreground
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
            }

            Grid {
                id: grid
                width: parent.width
                spacing: 14
                columns: Math.max(1, Math.floor((width + spacing) / (Theme.slideMinWidth + spacing)))

                Repeater {
                    model: root.controller.slides
                    delegate: SlideThumb {
                        required property int index
                        required property string group
                        required property color colour
                        required property string text

                        objectName: "slide-" + index
                        width: (grid.width - (grid.columns - 1) * grid.spacing) / grid.columns
                        number: index + 1
                        groupLabel: group
                        groupColor: colour
                        slideText: text
                        status: index === root.controller.liveSlide ? "live"
                              : index === root.controller.previewSlide ? "next" : ""
                        onClicked: {
                            root.controller.activateSlide(index)
                            root.slideClicked(index)
                        }
                    }
                }
            }
        }
    }

    LText {
        anchors.centerIn: parent
        visible: root.controller.slides.count === 0
        text: root.controller.itemTitle === "" ? qsTr("Select an item in the service plan to see its slides.")
                                                : qsTr("This item has no slides.")
        color: Theme.textMuted
    }
}
