import QtQuick

// Single-line search box with placeholder text.
Rectangle {
    id: root

    property alias text: input.text
    property string placeholder

    implicitWidth: 240
    implicitHeight: 32
    radius: 6
    color: Theme.window
    border.color: input.activeFocus ? Theme.next : Theme.border

    Accessible.role: Accessible.EditableText
    Accessible.name: placeholder

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.text
        selectionColor: Theme.next
        font.family: Theme.fontFamily
        font.pixelSize: 13
        clip: true
    }

    LText {
        anchors.fill: input
        verticalAlignment: Text.AlignVCenter
        visible: input.text === "" && !input.activeFocus
        text: root.placeholder
        color: Theme.textMuted
    }
}
