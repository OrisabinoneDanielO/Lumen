import QtQuick

// An empty, labelled region of the main window. Later batches replace each
// placeholder with the real panel.
Rectangle {
    id: root

    property string title
    property color fill: Theme.panel

    color: fill
    border.color: Theme.border
    border.width: 1

    Accessible.role: Accessible.Pane
    Accessible.name: title

    Text {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 16
        text: root.title
        color: Theme.textMuted
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.letterSpacing: 0.9
    }
}
