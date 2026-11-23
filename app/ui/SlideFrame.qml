import QtQuick
import Lumen.Slides

// A 16:9 framed picture of what an output shows (Preview or Live), drawn with
// the same SlideContent as the audience output. Empty text means the output
// is clear, shown as black.
Rectangle {
    id: root

    property string text
    property var slideStyle: ({})
    property string footer
    property url poster // a picture of the video or image shown
    property color background: Theme.slideBackground
    property color frameColor: Theme.border
    property int frameWidth: 2
    property int fontSize: 12   // unthemed text
    property bool bold: false   // unthemed text

    implicitHeight: Math.round(width * 9 / 16)
    radius: 8
    readonly property bool hasPoster: poster.toString() !== ""
    color: text !== "" && !hasPoster ? background : "#000000"
    border.color: frameColor
    border.width: frameWidth
    clip: true

    readonly property bool themed: slideStyle !== null && slideStyle !== undefined && slideStyle.size !== undefined

    Image {
        objectName: "poster"
        anchors.fill: parent
        anchors.margins: root.frameWidth
        visible: root.hasPoster
        source: root.poster
        fillMode: Image.PreserveAspectFit
        asynchronous: true
    }

    SlideContent {
        anchors.fill: parent
        anchors.margins: root.frameWidth
        visible: root.themed
        kind: root.text !== "" ? "text" : "empty"
        text: root.text
        style: root.slideStyle
        footer: root.footer
        layerName: "slide"
    }

    LText {
        anchors.fill: parent
        anchors.margins: 12 + root.frameWidth
        visible: !root.themed
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
