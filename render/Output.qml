import QtQuick
import QtQuick.Window

// The audience output: a background colour and centred text. Qt Quick draws
// it on the GPU through Qt RHI, with distance-field glyphs for the text.
// Batch 4 replaces this with the layer stack.
Window {
    id: output

    property color background: "#000000" // an output starts black
    property string text: ""

    visible: false // the C++ side places it on a screen, then shows it
    title: "Lumen Output"
    color: background

    Text {
        anchors.fill: parent
        anchors.margins: Math.round(parent.width * 0.06)
        text: output.text
        color: "#FFFFFF"
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: Math.max(12, Math.round(output.height * 0.07))
        font.weight: Font.DemiBold
        lineHeight: 1.2
    }
}
