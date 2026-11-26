import QtQuick
import QtQuick.Layouts

// Messages (batch 12, basic): a line of text in the banner at the top of the
// output ("Parent of child 42, please come to the nursery"), shown over the
// slides without touching them. Recent messages can be shown again with a
// click. Templates with fields arrive in batch 14.
Rectangle {
    id: root

    required property QtObject controller
    signal closeRequested()

    function focusInput() { input.focusInput() }

    width: 420
    height: column.implicitHeight + 24
    radius: 10
    color: Theme.window
    border.color: Theme.border

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            SectionLabel {
                Layout.fillWidth: true
                text: qsTr("MESSAGE")
            }
            LButton {
                text: "✕"
                variant: "ghost"
                implicitHeight: 28
                onClicked: root.closeRequested()
            }
        }
        LText {
            Layout.fillWidth: true
            visible: root.controller.messageShown !== ""
            text: qsTr("On screen: %1").arg(root.controller.messageShown)
            font.pixelSize: 11
            color: Theme.live
        }
        SearchField {
            id: input
            objectName: "messageInput"
            inputName: "messageText"
            Layout.fillWidth: true
            placeholder: qsTr("Message to show over the slides")
            onAccepted: showButton.clicked()
            onEscaped: root.closeRequested()
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            LButton {
                objectName: "messageHide"
                text: qsTr("Hide")
                implicitHeight: 30
                radius: 6
                onClicked: root.controller.hideMessage()
            }
            LButton {
                id: showButton
                objectName: "messageShow"
                text: qsTr("Show")
                variant: "primary"
                implicitHeight: 30
                radius: 6
                onClicked: {
                    if (root.controller.showMessage(input.text))
                        input.text = ""
                }
            }
        }
        SectionLabel {
            visible: root.controller.recentMessages.length > 0
            text: qsTr("RECENT")
        }
        Repeater {
            model: root.controller.recentMessages
            delegate: Rectangle {
                id: recent
                required property string modelData
                required property int index
                objectName: "recentMessage-" + index
                Layout.fillWidth: true
                implicitHeight: 30
                radius: 6
                color: recentMouse.containsMouse ? Theme.raised : "transparent"
                LText {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    verticalAlignment: Text.AlignVCenter
                    text: recent.modelData
                    font.pixelSize: 12
                }
                MouseArea {
                    id: recentMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.controller.showMessage(recent.modelData)
                }
            }
        }
    }
}
