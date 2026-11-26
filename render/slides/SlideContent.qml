import QtQuick

// Draws one piece of slide content. Shared by the audience output and the
// operator's Preview and Live frames.
//
// Themed text (style set, batch 8): placed in the theme's safe area at the
// fitted size scaled to this view (sizes are for 1080p), with a footer band,
// outline and shadow; Text.Fit shrinks it further only if it would overflow.
// Unthemed text keeps the per-layer placement from batch 4: Props as a lower
// third, Messages as a top banner, everything else centred.
Item {
    id: view
    objectName: "slideContent"

    property string kind: "empty" // empty | colour | text | image
    property color colour: "white"
    property string text
    property url image
    property var style: ({})      // TextStyle JSON plus "size"
    property string footer
    property string layerName

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
        visible: view.kind === "image"
        source: view.kind === "image" ? view.image : ""
        fillMode: view.layerName === "background" ? Image.PreserveAspectCrop : Image.PreserveAspectFit
        smooth: true
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

    Text {
        visible: !view.themed && view.kind === "text"
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
