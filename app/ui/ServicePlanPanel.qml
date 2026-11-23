import QtQuick
import QtQuick.Layouts

// Left column: the ordered items of today's service. Clicking an item shows
// its slides; it does not send anything live.
Rectangle {
    id: root

    required property QtObject controller
    signal itemClicked(int index)

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
                    model: root.controller.plan

                    delegate: Rectangle {
                        id: planRow
                        required property int index
                        required property string kind
                        required property string title
                        required property string subtitle
                        readonly property bool current: index === root.controller.currentItem
                        readonly property bool live: index === root.controller.liveItem

                        objectName: "planItem-" + index
                        width: planColumn.width
                        height: 50
                        radius: 8
                        color: current ? Theme.selected : rowMouse.containsMouse ? Theme.raised : "transparent"
                        Accessible.role: Accessible.ListItem
                        Accessible.name: (kind ? kind + ": " : "") + title + (live ? qsTr(", live") : "")

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 10

                            // Orange dot: the item that is live on screen.
                            Rectangle {
                                implicitWidth: 6
                                implicitHeight: 6
                                radius: 3
                                color: planRow.live ? Theme.live : "transparent"
                            }
                            SectionLabel {
                                Layout.preferredWidth: 52
                                text: planRow.kind
                                font.pixelSize: 10
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                LText {
                                    Layout.fillWidth: true
                                    text: planRow.title
                                    font.weight: Font.Medium
                                }
                                LText {
                                    Layout.fillWidth: true
                                    visible: text !== ""
                                    text: planRow.subtitle
                                    font.pixelSize: 11
                                    color: Theme.textMuted
                                }
                            }
                        }

                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.controller.selectItem(planRow.index)
                                root.itemClicked(planRow.index)
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
        visible: root.controller.plan.count === 0
        text: qsTr("No items yet. Pick a song in the library below and choose Add to plan.")
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        color: Theme.textMuted
    }
}
