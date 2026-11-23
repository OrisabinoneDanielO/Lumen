import QtQuick

// A 16:9 framed picture of what an output shows (Preview or Live).
// Empty text means the output is clear, shown as black.
Rectangle {
    id: root

    property string text
    property color frameColor: Theme.border
    property int frameWidth: 2
    property int fontSize: 12
    property bool bold: false

    implicitHeight: Math.round(width * 9 / 16)
    radius: 8
    color: text !== "" ? Theme.slideBackground : "#000000"
    border.color: frameColor
    border.width: frameWidth

    LText {
        anchors.fill: parent
        anchors.margins: 12 + root.frameWidth
        text: root.text
        color: "#FFFFFF"
        font.pixelSize: root.fontSize
        font.weight: root.bold ? Font.DemiBold : Font.Normal
        lineHeight: 1.35
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
