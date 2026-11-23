import QtQuick
import QtQuick.Layouts

// The stage display (batch 13): turn it on or off, pick its layout, and send
// a message to the stage only ("Repeat the last verse"); the audience never
// sees it.
Rectangle {
    id: root

    required property QtObject stage
    signal closeRequested()

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
                text: qsTr("STAGE DISPLAY")
            }
            LButton {
                objectName: "stageToggle"
                text: root.stage.connected ? qsTr("Turn off") : qsTr("Turn on")
                variant: root.stage.connected ? "default" : "primary"
                implicitHeight: 28
                radius: 6
                onClicked: root.stage.toggle()
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
            text: root.stage.connected
                  ? qsTr("Showing on the stage screen (or in a window when there is no third screen).")
                  : qsTr("Off. Turn it on to show current and next slides, the time and messages to the stage.")
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            font.pixelSize: 11
            color: Theme.textMuted
        }

        SectionLabel { text: qsTr("LAYOUT") }
        Repeater {
            model: root.stage.layouts
            delegate: Rectangle {
                id: choice
                required property int index
                required property string name
                required property string description
                objectName: "stageLayout-" + index
                Layout.fillWidth: true
                implicitHeight: 40
                radius: 6
                color: index === root.stage.layout ? Theme.selected : choiceMouse.containsMouse ? Theme.raised : "transparent"
                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    spacing: 0
                    LText {
                        text: choice.name
                        font.weight: Font.Medium
                        font.pixelSize: 12
                    }
                    LText {
                        text: choice.description
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                }
                MouseArea {
                    id: choiceMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.stage.layout = choice.index
                }
            }
        }

        SectionLabel { text: qsTr("MESSAGE TO THE STAGE") }
        LText {
            Layout.fillWidth: true
            visible: root.stage.message !== ""
            text: qsTr("On the stage: %1").arg(root.stage.message)
            font.pixelSize: 11
            color: Theme.live
        }
        SearchField {
            id: input
            objectName: "stageMessageInput"
            inputName: "stageMessageText"
            Layout.fillWidth: true
            placeholder: qsTr("e.g. Repeat the last verse")
            onAccepted: sendButton.clicked()
            onEscaped: root.closeRequested()
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            LButton {
                objectName: "stageMessageClear"
                text: qsTr("Clear")
                implicitHeight: 30
                radius: 6
                onClicked: root.stage.clearMessage()
            }
            LButton {
                id: sendButton
                objectName: "stageMessageSend"
                text: qsTr("Send to stage")
                variant: "primary"
                implicitHeight: 30
                radius: 6
                onClicked: {
                    if (root.stage.showMessage(input.text))
                        input.text = ""
                }
            }
        }
    }
}
