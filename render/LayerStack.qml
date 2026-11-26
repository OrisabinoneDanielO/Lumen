import QtQuick
import Lumen.Slides

// The six layers (bottom to top: Background, Media, Slide, Props, Messages,
// Masks), each fading between its outgoing and incoming content. Shared by
// the audience output and the stream output (batch 15), which shows only
// some layers (`include`, by name; empty means all).
Item {
    id: stack

    required property QtObject scene
    property var include: []

    Repeater {
        model: stack.scene.layers

        delegate: Item {
            id: layer

            required property QtObject modelData

            anchors.fill: parent
            visible: stack.include.length === 0 || stack.include.indexOf(modelData.name) >= 0
            z: modelData.index

            // Outgoing content: stays fully up under an opaque fill fading in
            // (an exact crossfade), otherwise fades out.
            SlideContent {
                readonly property QtObject content: layer.modelData.previous
                anchors.fill: parent
                kind: content.kind
                colour: content.colour
                text: content.text
                image: content.image
                style: content.style
                footer: content.footer
                video: content.video
                loop: content.loop
                volume: content.volume
                playVideo: layer.visible // a layer the output leaves out decodes nothing
                timerText: content.kind === "timer" ? stack.scene.timerText(content.timer, stack.scene.wallMs) : ""
                prop: content.prop
                elements: content.elements
                wallMs: stack.scene.wallMs
                layerName: layer.modelData.name
                opacity: layer.modelData.current.opaque ? 1.0 : 1.0 - layer.modelData.progress
                visible: opacity > 0 && layer.modelData.progress < 1.0
            }
            SlideContent {
                readonly property QtObject content: layer.modelData.current
                anchors.fill: parent
                kind: content.kind
                colour: content.colour
                text: content.text
                image: content.image
                style: content.style
                footer: content.footer
                video: content.video
                loop: content.loop
                volume: content.volume
                playVideo: layer.visible // a layer the output leaves out decodes nothing
                timerText: content.kind === "timer" ? stack.scene.timerText(content.timer, stack.scene.wallMs) : ""
                prop: content.prop
                elements: content.elements
                wallMs: stack.scene.wallMs
                layerName: layer.modelData.name
                opacity: layer.modelData.progress
            }
        }
    }

}
