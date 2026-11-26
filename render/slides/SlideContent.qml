import QtQuick
import QtMultimedia

// Draws one piece of slide content. Shared by the audience output and the
// operator's Preview and Live frames.
//
// Themed text (style set, batch 8): placed in the theme's safe area at the
// fitted size scaled to this view (sizes are for 1080p), with a footer band,
// outline and shadow; Text.Fit shrinks it further only if it would overflow.
// Unthemed text keeps the per-layer placement from batch 4: Props as a lower
// third, Messages as a top banner, everything else centred.
//
// Timers (batch 12): the time comes in as timerText, worked out by whoever
// shows this (the render process from its own clock). On the Messages layer
// a timer sits in the banner; elsewhere its label is above a large time.
//
// Props (batch 14): a lower third (name and title) at the bottom left and a
// bug (a logo) in a corner, either or both. Messages with style "ticker"
// scroll across the banner instead of standing still.
Item {
    id: view

    // A video here could not be played (missing file, unknown format).
    signal videoFailed(string path, string message)
    objectName: "slideContent"

    property string kind: "empty" // empty | colour | text | image | video | timer | prop | composed
    property color colour: "white"
    property string text
    property url image
    property var style: ({})      // TextStyle JSON plus "size"
    property string footer
    property string layerName
    // Video: the renderer plays it; the operator's frames show its poster.
    property url video
    property bool loop: false
    property real volume: 1.0
    property bool playVideo: true
    property string timerText
    property var prop: ({})
    // Composed slides (batch 16): positioned elements, and the wall clock
    // their clocks and timers count from.
    property var elements: []
    property real wallMs: Date.now()
    readonly property var lowerThird: kind === "prop" && prop && prop.lowerThird ? prop.lowerThird : null
    readonly property var bug: kind === "prop" && prop && prop.bug ? prop.bug : null
    readonly property bool ticker: kind === "text" && isMessage && style !== null && style !== undefined && style.ticker === true

    readonly property bool themed: kind === "text" && style !== null && style !== undefined
                                   && style.size !== undefined
    readonly property real unit: height / 1080
    readonly property real margin: Math.round(width * 0.06)
    readonly property bool isProps: layerName === "props"
    readonly property bool isMessage: layerName === "messages"

    function hAlign(value) {
        return value === "left" ? Text.AlignLeft : value === "right" ? Text.AlignRight : Text.AlignHCenter
    }
    function vAlign(value) {
        return value === "top" ? Text.AlignTop : value === "bottom" ? Text.AlignBottom : Text.AlignVCenter
    }

    Rectangle {
        anchors.fill: parent
        visible: view.kind === "colour"
        color: view.kind === "colour" ? view.colour : "transparent"
    }

    Image {
        anchors.fill: parent
        visible: view.kind === "image" || (view.kind === "video" && !view.playVideo)
        source: view.kind === "image" || view.kind === "video" ? view.image : ""
        fillMode: view.layerName === "background" ? Image.PreserveAspectCrop : Image.PreserveAspectFit
        smooth: true
    }

    // Video, decoded here in the render process (hardware decode where the
    // GPU supports the codec). Motion backgrounds loop silently.
    //
    // A video that leaves is hidden and muted at once and its player is
    // destroyed a moment later: shutting a decoder down can take a second or
    // more (software decode on macOS), and the next frame must not wait.
    Item {
        id: videoHost
        readonly property bool wanted: view.kind === "video" && view.playVideo && view.video.toString() !== ""
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
                id: videoItem
                anchors.fill: parent
                function retire() {
                    visible = false
                    sound.muted = true
                    player.source = player.source // keeps the source when the slide's goes
                    destroy(300)
                }
                MediaPlayer {
                    id: player
                    source: view.video
                    loops: view.loop ? MediaPlayer.Infinite : 1
                    videoOutput: output
                    audioOutput: AudioOutput {
                        id: sound
                        volume: view.volume
                        muted: view.volume <= 0
                    }
                    Component.onCompleted: play()
                    onErrorOccurred: (error, errorString) => view.videoFailed(view.video.toString(), errorString)
                }
                VideoOutput {
                    id: output
                    objectName: "videoOutput"
                    // Read by the renderer's frame monitor (microseconds).
                    property real mediaDurationUs: player.duration * 1000
                    anchors.fill: parent
                    fillMode: view.layerName === "background" ? VideoOutput.PreserveAspectCrop : VideoOutput.PreserveAspectFit
                }
            }
        }
    }

    // ---- unthemed text (batch 4 placement) --------------------------------

    Rectangle {
        visible: !view.themed && view.kind === "text" && (view.isProps || view.isMessage)
        x: 0
        y: view.isMessage ? 0 : Math.round(view.height * 0.75)
        width: view.width
        height: Math.round(view.height * (view.isMessage ? 0.12 : 0.2))
        color: "#B3000000"
    }

    // A ticker: the message scrolls across the banner, again and again.
    Item {
        visible: view.ticker
        width: view.width
        height: Math.round(view.height * 0.12)
        clip: true
        Text {
            id: tickerText
            objectName: "tickerText"
            anchors.verticalCenter: parent.verticalCenter
            text: view.ticker ? view.text : ""
            color: view.colour
            font.family: "Noto Sans"
            font.pixelSize: Math.max(12, Math.round(view.height * 0.05))
            font.weight: Font.DemiBold
            NumberAnimation on x {
                running: view.ticker && tickerText.implicitWidth > 0
                loops: Animation.Infinite
                from: view.width
                to: -tickerText.implicitWidth
                // About a fifth of the screen width per second.
                duration: Math.max(1000, Math.round((view.width + tickerText.implicitWidth) / Math.max(1, view.width * 0.2) * 1000))
            }
        }
    }

    Text {
        visible: !view.themed && view.kind === "text" && !view.ticker
        x: view.margin
        y: view.isMessage ? 0 : view.isProps ? Math.round(view.height * 0.75) : view.margin
        width: view.width - 2 * view.margin
        height: view.isMessage ? Math.round(view.height * 0.12)
              : view.isProps ? Math.round(view.height * 0.2)
              : view.height - 2 * view.margin
        text: view.kind === "text" ? view.text : ""
        color: view.kind === "text" ? view.colour : "white"
        wrapMode: Text.WordWrap
        horizontalAlignment: view.isProps ? Text.AlignLeft : Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        fontSizeMode: Text.Fit
        minimumPixelSize: 10
        font.family: "Noto Sans" // bundled, so every OS draws the same
        font.pixelSize: Math.max(12, Math.round(view.height * (view.isMessage ? 0.05 : view.isProps ? 0.06 : 0.07)))
        font.weight: Font.DemiBold
        lineHeight: 1.2
    }

    // ---- composed slides (the editor) ----------------------------------------

    ComposedSlide {
        onVideoFailed: (path, message) => view.videoFailed(path, message)
        anchors.fill: parent
        visible: view.kind === "composed"
        elements: view.kind === "composed" ? view.elements : []
        wallMs: view.wallMs
        playVideo: view.playVideo
    }

    // ---- props -----------------------------------------------------------------

    // Lower third: a dark band with an accent bar, the name above the title.
    Rectangle {
        id: lowerThirdBand
        objectName: "lowerThird"
        visible: view.lowerThird !== null
        x: view.margin
        y: Math.round(view.height * 0.74)
        width: Math.min(view.width - 2 * view.margin, lowerThirdColumn.implicitWidth + Math.round(view.height * 0.08))
        height: lowerThirdColumn.implicitHeight + Math.round(view.height * 0.04)
        color: "#E6171A20"
        Rectangle {
            width: Math.max(2, Math.round(view.height * 0.008))
            height: parent.height
            color: "#F08A24"
        }
        Column {
            id: lowerThirdColumn
            x: Math.round(view.height * 0.04)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Math.round(view.height * 0.004)
            Text {
                objectName: "lowerThirdLine1"
                text: view.lowerThird ? (view.lowerThird.line1 || "") : ""
                color: "white"
                font.family: "Noto Sans"
                font.weight: Font.Bold
                font.pixelSize: Math.max(10, Math.round(view.height * 0.05))
            }
            Text {
                objectName: "lowerThirdLine2"
                visible: text !== ""
                text: view.lowerThird ? (view.lowerThird.line2 || "") : ""
                color: "#F2C38B"
                font.family: "Noto Sans"
                font.pixelSize: Math.max(8, Math.round(view.height * 0.032))
            }
        }
    }

    // Bug: a logo in a corner.
    Image {
        objectName: "bug"
        visible: view.bug !== null
        readonly property real inset: Math.round(view.height * 0.04)
        readonly property string corner: view.bug ? (view.bug.corner || "topRight") : "topRight"
        height: Math.round(view.height * (view.bug && view.bug.size ? view.bug.size : 0.12))
        width: height * Math.max(0.2, sourceSize.width > 0 ? sourceSize.width / Math.max(1, sourceSize.height) : 1)
        x: corner.endsWith("Left") ? inset : view.width - width - inset
        y: corner.startsWith("top") ? inset : view.height - height - inset
        source: view.bug && view.bug.image ? view.bug.image : "" // a file:// URL
        fillMode: Image.PreserveAspectFit
        smooth: true
        opacity: 0.9
    }

    // ---- timers ---------------------------------------------------------------

    Rectangle {
        visible: view.kind === "timer" && view.isMessage
        width: view.width
        height: Math.round(view.height * 0.12)
        color: "#B3000000"
    }
    Text {
        objectName: "timerBanner"
        visible: view.kind === "timer" && view.isMessage
        x: view.margin
        width: view.width - 2 * view.margin
        height: Math.round(view.height * 0.12)
        text: view.kind === "timer" ? (view.text !== "" ? view.text + "   " + view.timerText : view.timerText) : ""
        color: "white"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        fontSizeMode: Text.Fit
        minimumPixelSize: 10
        font.family: "Noto Sans"
        font.pixelSize: Math.max(12, Math.round(view.height * 0.06))
        font.weight: Font.DemiBold
        font.features: { "tnum": 1 } // digits of equal width: the time does not jitter
    }
    Column {
        visible: view.kind === "timer" && !view.isMessage
        anchors.centerIn: parent
        width: view.width - 2 * view.margin
        spacing: Math.round(view.height * 0.02)
        Text {
            width: parent.width
            visible: text !== ""
            text: view.kind === "timer" ? view.text : ""
            color: "white"
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.family: "Noto Sans"
            font.weight: Font.DemiBold
            font.pixelSize: Math.max(10, Math.round(view.height * 0.07))
            style: Text.Outline
            styleColor: "#80000000"
        }
        Text {
            objectName: "timerText"
            width: parent.width
            text: view.kind === "timer" ? view.timerText : ""
            color: "white"
            horizontalAlignment: Text.AlignHCenter
            font.family: "Noto Sans"
            font.weight: Font.Bold
            font.pixelSize: Math.max(12, Math.round(view.height * 0.24))
            font.features: { "tnum": 1 }
            style: Text.Outline
            styleColor: "#80000000"
        }
    }

    // ---- themed text ---------------------------------------------------------

    Item {
        id: themedText
        anchors.fill: parent
        visible: view.themed

        readonly property var s: view.themed ? view.style : ({})
        readonly property var area: s.safeArea !== undefined ? s.safeArea : ({ x: 0.05, y: 0.05, w: 0.9, h: 0.9 })
        readonly property rect safe: Qt.rect(area.x * view.width, area.y * view.height,
                                             area.w * view.width, area.h * view.height)
        readonly property bool hasFooter: view.footer !== "" && s.footerPosition !== "none"
        readonly property real footerHeight: hasFooter ? Math.ceil((s.footerSize || 28) * 1.6 * view.unit) : 0
        readonly property real gap: hasFooter ? Math.ceil((s.footerSize || 28) * 0.5 * view.unit) : 0
        readonly property bool footerAbove: s.footerPosition === "above"
        readonly property rect bodyRect: Qt.rect(safe.x, footerAbove ? safe.y + footerHeight + gap : safe.y,
                                             safe.width, safe.height - footerHeight - gap)
        readonly property real shadowOffset: Math.max(1, Math.round(3 * view.unit))

        // Shadow: the same text, dark and offset, behind the body.
        Text {
            visible: themedText.s.shadow === true
            x: body.x + themedText.shadowOffset
            y: body.y + themedText.shadowOffset
            width: body.width
            height: body.height
            text: body.text
            color: "#99000000"
            font: body.font
            wrapMode: body.wrapMode
            horizontalAlignment: body.horizontalAlignment
            verticalAlignment: body.verticalAlignment
            fontSizeMode: body.fontSizeMode
            minimumPixelSize: body.minimumPixelSize
            lineHeight: body.lineHeight
        }

        Text {
            id: body
            objectName: "bodyText"
            x: themedText.bodyRect.x
            y: themedText.bodyRect.y
            width: themedText.bodyRect.width
            height: themedText.bodyRect.height
            text: view.themed ? view.text : ""
            color: themedText.s.colour || "white"
            font.family: themedText.s.font || "Noto Sans"
            font.weight: themedText.s.weight || 600
            font.pixelSize: Math.max(1, Math.round((themedText.s.size || 72) * view.unit))
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            horizontalAlignment: view.hAlign(themedText.s.align)
            verticalAlignment: view.vAlign(themedText.s.valign)
            // The size was fitted in advance; this only shrinks text that would
            // still overflow (e.g. a different output shape).
            fontSizeMode: Text.Fit
            minimumPixelSize: Math.max(1, Math.round((themedText.s.minSize || 36) * view.unit * 0.8))
            lineHeight: themedText.s.lineHeight || 1.2
            style: (themedText.s.outline || 0) > 0 ? Text.Outline : Text.Normal
            styleColor: themedText.s.outlineColour || "black"
        }

        Text {
            objectName: "footerText"
            visible: themedText.hasFooter
            x: themedText.safe.x
            y: themedText.footerAbove ? themedText.safe.y : themedText.safe.y + themedText.safe.height - themedText.footerHeight
            width: themedText.safe.width
            height: themedText.footerHeight
            text: view.footer
            color: themedText.s.footerColour || "#F2C38B"
            font.family: body.font.family
            font.weight: Font.DemiBold
            font.pixelSize: Math.max(1, Math.round((themedText.s.footerSize || 28) * view.unit))
            horizontalAlignment: body.horizontalAlignment
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: Math.max(1, Math.round((themedText.s.footerSize || 28) * view.unit * 0.6))
            style: body.style
            styleColor: body.styleColor
        }
    }
}
