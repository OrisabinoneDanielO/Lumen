import QtQuick
import Lumen.Slides

// One slide card in the slide grid: group label, LIVE/NEXT tag, 16:9 preview.
Rectangle {
    id: root

    property int number
    property string groupLabel
    property color groupColor: Theme.textMuted
    property string slideText
    required property url thumbnail // media slides: a picture of the video or image
    property var elements: []        // a slide laid out by position: drawn as it is
    signal clicked()
    property string status // "live", "next" or ""

    implicitHeight: header.height + slide.height + 4
    radius: 10
    color: Theme.panel
    border.width: 2
    border.color: status === "live" ? Theme.live : status === "next" ? Theme.next : Theme.border

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Send slide %1 live").arg(number)

    Item {
        id: header
        x: 2
        y: 2
        width: parent.width - 4
        height: 28

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 8
                radius: 2
                color: root.groupColor
            }
            LText {
                text: root.groupLabel
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
        }

        LText {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: root.status === "live" ? qsTr("LIVE") : root.status === "next" ? qsTr("NEXT") : ""
            font.pixelSize: 10
            font.bold: true
            font.letterSpacing: 0.6
            color: root.status === "live" ? Theme.live : Theme.link
        }
    }

    Rectangle {
        id: slide
        x: 2
        anchors.top: header.bottom
        width: parent.width - 4
        height: Math.round(width * 9 / 16)
        radius: 8
        color: Theme.slideBackground
        // Long lyrics or scripture stay inside the card.
        clip: true

        // Square off the top corners so only the card's bottom corners round.
        Rectangle {
            width: parent.width
            height: parent.radius
            color: parent.color
        }

        Image {
            objectName: "thumbnail"
            anchors.fill: parent
            visible: root.thumbnail.toString() !== ""
            source: root.thumbnail
            fillMode: Image.PreserveAspectFit
            asynchronous: true
        }
        ComposedSlide {
            objectName: "thumbComposed"
            anchors.fill: parent
            visible: root.elements && root.elements.length > 0
            elements: visible ? root.elements : []
            playVideo: false
        }
        // Words: as large as fits the card (long lyrics and scripture shrink
        // rather than run off it).
        LText {
            objectName: "thumbText"
            anchors.fill: parent
            anchors.margins: 8
            visible: !(root.elements && root.elements.length > 0)
            text: root.slideText
            color: "#FFFFFF"
            font.pixelSize: 12
            fontSizeMode: Text.Fit
            minimumPixelSize: 6
            lineHeight: 1.2
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // Feedback: a light veil while pointed at, darker while pressed.
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: thumbArea.pressed ? "#33000000" : thumbArea.containsMouse ? "#14FFFFFF" : "transparent"
    }

    MouseArea {
        id: thumbArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
