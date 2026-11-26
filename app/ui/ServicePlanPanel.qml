import QtQuick
import QtQuick.Layouts

// Left column: the ordered items of today's service.
Rectangle {
    id: root

    // Each item: { kind, title, subtitle }
    property var items: []
    property int currentIndex: -1

    color: Theme.panel

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: 1
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 14
            Layout.bottomMargin: 10

            SectionLabel {
                Layout.fillWidth: true
                text: qsTr("SERVICE PLAN")
            }
            LButton {
                text: qsTr("+ Add")
                variant: "ghost"
                implicitHeight: 32
                radius: 6
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: planColumn.height + 12
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: planColumn
                x: 8
                width: parent.width - 16
                spacing: 2

                Repeater {
                    model: root.items

                    delegate: Rectangle {
                        id: planRow
                        required property var modelData
                        required property int index
                        readonly property bool current: index === root.currentIndex

                        width: planColumn.width
                        height: 50
                        radius: 8
                        color: current ? Theme.selected : "transparent"
                        Accessible.role: Accessible.ListItem
                        Accessible.name: modelData.kind + ": " + modelData.title

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 10

                            Rectangle {
                                implicitWidth: 6
                                implicitHeight: 6
                                radius: 3
                                color: planRow.current ? Theme.live : "transparent"
                            }
                            SectionLabel {
                                Layout.preferredWidth: 52
                                text: planRow.modelData.kind
                                font.pixelSize: 10
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                LText {
                                    Layout.fillWidth: true
                                    text: planRow.modelData.title
                                    font.weight: Font.Medium
                                }
                                LText {
                                    Layout.fillWidth: true
                                    text: planRow.modelData.subtitle
                                    font.pixelSize: 11
                                    color: Theme.textMuted
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    LText {
        anchors.centerIn: parent
        width: parent.width - 48
        visible: root.items.length === 0
        text: qsTr("No items yet. Drag songs, scripture and media here from the library.")
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        color: Theme.textMuted
    }
}
