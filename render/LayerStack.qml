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

            // The transition in progress (scene.h): Fade crossfades; Push
            // slides the new content in from the right (Push up: from below)
            // as the old slides out; Wipe uncovers the new from left to right
            // over the old; Zoom grows the new in as the old fades.
            readonly property string kind: modelData.transition
            readonly property real p: modelData.progress

            // Outgoing content: stays fully up under an opaque fill fading in
            // (an exact crossfade), otherwise fades out; pushed out, or left
            // under a wipe.
            SlideContent {
                readonly property QtObject content: layer.modelData.previous
                objectName: "layerPrevious-" + layer.modelData.name
                width: layer.width
                height: layer.height
                x: layer.kind === "push" ? -layer.width * layer.p : 0
                y: layer.kind === "pushUp" ? -layer.height * layer.p : 0
                kind: content.kind
                colour: content.colour
                text: content.text
                image: content.image
                style: content.style
                footer: content.footer
                video: content.video
                loop: content.loop
                trimInMs: content.trimInMs
                trimOutMs: content.trimOutMs
                volume: content.volume
                playVideo: layer.visible // a layer the output leaves out decodes nothing
                timerText: content.kind === "timer" ? stack.scene.timerText(content.timer, stack.scene.wallMs) : ""
                prop: content.prop
                elements: content.elements
                wallMs: stack.scene.wallMs
                layerName: layer.modelData.name
                onVideoFailed: (path, message) => stack.scene.reportVideoError(path, message)
                opacity: layer.kind === "push" || layer.kind === "pushUp" || layer.kind === "wipe" ? 1.0
                         : layer.kind === "fade" && layer.modelData.current.opaque ? 1.0
                         : 1.0 - layer.p
                visible: opacity > 0 && layer.p < 1.0
            }
            // Incoming content (in a clip for the wipe).
            Item {
                objectName: "layerClip-" + layer.modelData.name
                height: layer.height
                width: layer.kind === "wipe" ? layer.width * layer.p : layer.width
                clip: layer.kind === "wipe" && layer.p < 1.0
            SlideContent {
                readonly property QtObject content: layer.modelData.current
                id: currentContent
                transport: layer.modelData.name === "media"
                objectName: "layerCurrent-" + layer.modelData.name
                width: layer.width
                height: layer.height
                x: layer.kind === "push" ? layer.width * (1.0 - layer.p) : 0
                y: layer.kind === "pushUp" ? layer.height * (1.0 - layer.p) : 0
                scale: layer.kind === "zoom" ? 0.8 + 0.2 * layer.p : 1.0
                kind: content.kind
                colour: content.colour
                text: content.text
                image: content.image
                style: content.style
                footer: content.footer
                video: content.video
                loop: content.loop
                trimInMs: content.trimInMs
                trimOutMs: content.trimOutMs
                volume: content.volume
                playVideo: layer.visible // a layer the output leaves out decodes nothing
                timerText: content.kind === "timer" ? stack.scene.timerText(content.timer, stack.scene.wallMs) : ""
                prop: content.prop
                elements: content.elements
                wallMs: stack.scene.wallMs
                layerName: layer.modelData.name
                onVideoFailed: (path, message) => stack.scene.reportVideoError(path, message)
                onVideoPosition: (path, positionMs, durationMs, playing) => stack.scene.reportVideoPosition(path, positionMs, durationMs, playing)
                Connections {
                    target: currentContent.transport ? stack.scene : null
                    function onVideoCommand(action, positionMs) { currentContent.videoCommand(action, positionMs) }
                }
                opacity: layer.kind === "push" || layer.kind === "pushUp" || layer.kind === "wipe" ? 1.0 : layer.p
            }
            }
        }
    }

}
