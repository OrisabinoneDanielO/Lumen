import QtQuick

// Draws one piece of content (a ContentModel) for one layer. Text is placed
// by layer: Props as a lower third, Messages as a top banner, everything else
// centred.
Item {
    id: view

    property QtObject content
    property string layerName

    readonly property string kind: content ? content.kind : "empty"
    readonly property real margin: Math.round(width * 0.06)
    readonly property bool isProps: layerName === "props"
    readonly property bool isMessage: layerName === "messages"

    Rectangle {
        anchors.fill: parent
        visible: view.kind === "colour"
        color: view.kind === "colour" ? view.content.colour : "transparent"
    }

    Image {
        anchors.fill: parent
        visible: view.kind === "image"
        source: view.kind === "image" ? view.content.image : ""
        fillMode: view.layerName === "background" ? Image.PreserveAspectCrop : Image.PreserveAspectFit
        smooth: true
    }

    // Band behind lower-third and banner text so it reads over anything.
    Rectangle {
        visible: view.kind === "text" && (view.isProps || view.isMessage)
        x: 0
        y: view.isMessage ? 0 : Math.round(view.height * 0.75)
        width: view.width
        height: Math.round(view.height * (view.isMessage ? 0.12 : 0.2))
        color: "#B3000000"
    }

    Text {
        visible: view.kind === "text"
        x: view.margin
        y: view.isMessage ? 0 : view.isProps ? Math.round(view.height * 0.75) : view.margin
        width: view.width - 2 * view.margin
        height: view.isMessage ? Math.round(view.height * 0.12)
              : view.isProps ? Math.round(view.height * 0.2)
              : view.height - 2 * view.margin
        text: view.kind === "text" ? view.content.text : ""
        color: view.kind === "text" ? view.content.colour : "white"
        wrapMode: Text.WordWrap
        horizontalAlignment: view.isProps ? Text.AlignLeft : Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        fontSizeMode: Text.Fit
        minimumPixelSize: 10
        font.pixelSize: Math.max(12, Math.round(view.height * (view.isMessage ? 0.05 : view.isProps ? 0.06 : 0.07)))
        font.weight: Font.DemiBold
        lineHeight: 1.2
    }
}
