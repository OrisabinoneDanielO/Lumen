import QtQuick

// Removes something in two clicks: "Remove", then "Sure?" within a few
// seconds. A stray click never deletes a timer, prop or cue in a service.
LButton {
    id: button

    signal confirmed()
    property bool armed: false

    implicitHeight: 28
    radius: 6
    fontSize: 11
    variant: armed ? "warn" : "ghost"
    text: armed ? qsTr("Sure?") : qsTr("Remove")
    Accessible.name: armed ? qsTr("Click again to remove") : qsTr("Remove")
    onClicked: {
        if (armed) {
            armed = false
            confirmed()
        } else {
            armed = true
        }
    }

    Timer {
        running: button.armed
        interval: 3000
        onTriggered: button.armed = false
    }
}
