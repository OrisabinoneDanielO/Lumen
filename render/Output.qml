import QtQuick
import QtQuick.Window
import QtMultimedia
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

    LayerStack {
        anchors.fill: parent
        scene: output.scene
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
            // A video logo loops, silent, and decodes only while it shows.
            video: content.video
            loop: true
            volume: 0
            playVideo: output.scene.logoLevel > 0
            layerName: "logo"
        }
    }

    // Videos opened ahead of use (the item in Preview): decoded to their
    // first frame and paused, out of sight, so going live starts at once.
    Repeater {
        model: output.scene.preloads
        delegate: Item {
            required property url modelData
            width: 2
            height: 2
            opacity: 0
            MediaPlayer {
                source: parent.modelData
                videoOutput: warm
                Component.onCompleted: { play(); pause() }
                // Found out ahead: the operator hears before it goes live.
                onErrorOccurred: (error, errorString) => output.scene.reportVideoError(String(source), errorString)
            }
            VideoOutput {
                id: warm
                anchors.fill: parent
            }
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

    // While the operator sets up the screens: which screen this is.
    IdentifyLabel {
        identify: output.scene.identify
    }
}
