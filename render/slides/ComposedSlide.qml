import QtQuick
import QtMultimedia
import "timertext.js" as TimerText

// A slide made in the editor (batch 16): positioned elements on a 1920 x 1080
// grid, scaled to this item, bottom first. Shared by the audience output, the
// operator's Preview and Live frames and the editor's canvas.
//
// Element types: text, shape (a rectangle or ellipse), image, video, clock
// (the time of day) and timer (a countdown or count-up, see timertext.js).
// Styles are in 1080p pixels: text {font, size, bold, italic, colour, align,
// valign, shadow, outline, fit}, shape {fill, shape, radius, border,
// borderWidth}, image and video {fit: "contain" | "cover"}, video {loop,
// muted}, clock {format}.
Item {
    id: root

    property var elements: []
    // Wall-clock time for clocks and timers (ms since 1970); the render
    // process passes its own clock.
    property real wallMs: Date.now()
    property bool playVideo: true

    readonly property real sx: width / 1920
    readonly property real sy: height / 1080

    function hAlign(value) {
        return value === "left" ? Text.AlignLeft : value === "right" ? Text.AlignRight : Text.AlignHCenter
    }
    function vAlign(value) {
        return value === "top" ? Text.AlignTop : value === "bottom" ? Text.AlignBottom : Text.AlignVCenter
    }

    // Text, clock and timer elements share these settings.
    component StyledText: Text {
        property var s: ({})
        color: s.colour || "white"
        font.family: s.font || "Noto Sans"
        font.pixelSize: Math.max(1, Math.round((s.size || 64) * root.sy))
        font.bold: s.bold === true
        font.italic: s.italic === true
        horizontalAlignment: root.hAlign(s.align)
        verticalAlignment: root.vAlign(s.valign)
        wrapMode: Text.WordWrap
        fontSizeMode: s.fit === false ? Text.FixedSize : Text.Fit
        minimumPixelSize: Math.max(1, Math.round(12 * root.sy))
        style: (s.outline || 0) > 0 ? Text.Outline : s.shadow === true ? Text.Raised : Text.Normal
        styleColor: (s.outline || 0) > 0 ? (s.outlineColour || "black") : "#99000000"
    }

    Repeater {
        model: root.elements
        delegate: Item {
            id: element
            required property var modelData
            required property int index
            readonly property var frame: modelData.frame || [0, 0, 1920, 1080]
            readonly property var s: modelData.style || ({})
            objectName: "element-" + index + "-" + modelData.type
            x: frame[0] * root.sx
            y: frame[1] * root.sy
            width: frame[2] * root.sx
            height: frame[3] * root.sy

            StyledText {
                objectName: "elementText-" + element.index
                anchors.fill: parent
                visible: element.modelData.type === "text" || element.modelData.type === "clock"
                         || element.modelData.type === "timer"
                s: element.s
                font.features: element.modelData.type === "text" ? ({}) : { "tnum": 1 }
                text: element.modelData.type === "text" ? (element.modelData.text || "")
                    : element.modelData.type === "clock" ? Qt.formatTime(new Date(root.wallMs), element.s.format || "hh:mm")
                    : element.modelData.type === "timer" ? (element.modelData.text ? element.modelData.text + " " : "")
                                                           + TimerText.timerText(element.modelData.timer, root.wallMs)
                    : ""
            }

            Rectangle {
                anchors.fill: parent
                visible: element.modelData.type === "shape"
                color: element.s.fill || "#F08A24"
                radius: element.s.shape === "ellipse" ? Math.min(width, height) / 2 : (element.s.radius || 0) * root.sy
                border.color: element.s.border || "transparent"
                border.width: (element.s.borderWidth || 0) * root.sy
            }

            Image {
                anchors.fill: parent
                visible: element.modelData.type === "image"
                source: element.modelData.type === "image" ? (element.modelData.image || "") : ""
                fillMode: element.s.fit === "cover" ? Image.PreserveAspectCrop : Image.PreserveAspectFit
                clip: element.s.fit === "cover"
                asynchronous: true
                smooth: true
            }

            // Video: played where the slide is shown live; a dark box with a
            // play sign elsewhere (the editor, Preview).
            Rectangle {
                anchors.fill: parent
                visible: element.modelData.type === "video" && !root.playVideo
                color: "#80000000"
                Text {
                    anchors.centerIn: parent
                    text: "▶"
                    color: "white"
                    font.pixelSize: Math.max(10, parent.height * 0.3)
                }
            }
            // As in SlideContent: a leaving video is hidden and muted at once
            // and its player destroyed a moment later, so a slow decoder
            // shutdown cannot hold the next frame.
            Item {
                id: videoHost
                readonly property bool wanted: element.modelData.type === "video" && root.playVideo
                                               && (element.modelData.video || "") !== ""
                property Item current: null
                anchors.fill: parent

                function refresh() {
                    if (current && !wanted) {
                        const leaving = current
                        current = null
                        leaving.retire()
                    }
                    if (wanted && !current)
                        current = videoComponent.createObject(videoHost)
                }
                onWantedChanged: refresh()
                Component.onCompleted: refresh()

                Component {
                    id: videoComponent
                    Item {
                        anchors.fill: parent
                        function retire() {
                            visible = false
                            sound.muted = true
                            player.source = player.source
                            destroy(300)
                        }
                        MediaPlayer {
                            id: player
                            source: element.modelData.video
                            loops: element.s.loop === false ? 1 : MediaPlayer.Infinite
                            videoOutput: output
                            audioOutput: AudioOutput {
                                id: sound
                                muted: element.s.muted !== false
                            }
                            Component.onCompleted: play()
                        }
                        VideoOutput {
                            id: output
                            objectName: "videoOutput"
                            property real mediaDurationUs: player.duration * 1000
                            anchors.fill: parent
                            fillMode: element.s.fit === "cover" ? VideoOutput.PreserveAspectCrop : VideoOutput.PreserveAspectFit
                        }
                    }
                }
            }
        }
    }
}
