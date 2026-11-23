import QtQuick
import QtQuick.Layouts

// Centre region: the selected item's title, arrangement and slide cards.
Item {
    id: root

    property string title
    property string detail
    // Each chip: { label, background, foreground }
    property var arrangement: []
    // Each slide: { group, color, text }
    property var slides: []
    property int liveIndex: -1
    property int nextIndex: -1

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
                visible: root.title !== ""
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    LText {
                        Layout.fillWidth: true
                        text: root.title
                        font.pixelSize: 22
                        font.bold: true
                    }
                    LText {
                        Layout.fillWidth: true
                        text: root.detail
                        color: Theme.textMuted
                    }
                }

                Row {
                    Layout.alignment: Qt.AlignBottom
                    spacing: 6

                    LText {
                        anchors.verticalCenter: parent.verticalCenter
                        rightPadding: 4
                        text: qsTr("Arrangement")
                        font.pixelSize: 12
                        color: Theme.textMuted
                    }
                    Repeater {
                        model: root.arrangement
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
                    model: root.slides
                    delegate: SlideThumb {
                        required property var modelData
                        required property int index
                        width: (grid.width - (grid.columns - 1) * grid.spacing) / grid.columns
                        number: index + 1
                        group: modelData.group
                        groupColor: modelData.color
                        text: modelData.text
                        status: index === root.liveIndex ? "live"
                              : index === root.nextIndex ? "next" : ""
                    }
                }
            }
        }
    }

    LText {
        anchors.centerIn: parent
        visible: root.slides.length === 0
        text: qsTr("Select an item in the service plan to see its slides.")
        color: Theme.textMuted
    }
}
