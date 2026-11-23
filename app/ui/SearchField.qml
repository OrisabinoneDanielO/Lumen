import QtQuick

// Single-line search box with placeholder text. Escape leaves it; Return
// accepts it.
Rectangle {
    id: root

    property alias text: input.text
    property string placeholder
    readonly property bool inputFocused: input.activeFocus
    signal escaped()
    signal accepted()

    function focusInput() { input.forceActiveFocus() }

    implicitWidth: 240
    implicitHeight: 32
    radius: 6
    color: Theme.window
    border.color: input.activeFocus ? Theme.next : Theme.border

    Accessible.role: Accessible.EditableText
    Accessible.name: placeholder

    TextInput {
        id: input
        objectName: "searchInput"
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.text
        selectionColor: Theme.next
        font.family: Theme.fontFamily
        font.pixelSize: 13
        clip: true
        Keys.onReturnPressed: root.accepted()
        Keys.onEnterPressed: root.accepted()
        Keys.onEscapePressed: {
            input.focus = false
            root.escaped()
        }
    }

    LText {
        anchors.fill: input
        verticalAlignment: Text.AlignVCenter
        visible: input.text === "" && !input.activeFocus
        text: root.placeholder
        color: Theme.textMuted
    }
}
