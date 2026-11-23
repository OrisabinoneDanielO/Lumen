import QtQuick
import QtQuick.Window
import Lumen.Slides

// The audience output: six layers (bottom to top: Background, Media, Slide,
// Props, Messages, Masks) and the Logo and Black overrides above them. Qt
// Quick draws it on the GPU through Qt RHI. Fades are driven by the
// SceneModel's clock, not by the operator UI.
Window {
    id: output

    required property QtObject scene

    visible: false // the C++ side places it on a screen, then shows it
    title: "Lumen Output"
    color: "#000000" // an output starts black

    Repeater {
        model: output.scene.layers

        delegate: Item {
            id: layer

            required property QtObject modelData

            anchors.fill: parent
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
                layerName: layer.modelData.name
                opacity: layer.modelData.progress
            }
        }
    }

    // Logo: the logo content on black, above every layer.
    Rectangle {
        z: 100
        anchors.fill: parent
        color: "#000000"
        opacity: output.scene.logoLevel
        visible: opacity > 0

        SlideContent {
            readonly property QtObject content: output.scene.logo
            anchors.fill: parent
            kind: content.kind
            colour: content.colour
            text: content.text
            image: content.image
            style: content.style
            footer: content.footer
            layerName: "logo"
        }
    }

    // Black: above everything, including Logo.
    Rectangle {
        z: 101
        anchors.fill: parent
        color: "#000000"
        opacity: output.scene.blackLevel
        visible: opacity > 0
    }
}
