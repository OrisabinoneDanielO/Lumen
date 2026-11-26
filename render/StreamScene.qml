import QtQuick

// The stream output (batch 15): only the Slide and Props layers, on
// transparency, for streaming software and switchers (NDI, or key and fill).
// Rendered off screen by the stream renderer; Black takes everything away
// (to full transparency), Logo does not apply.
Item {
    id: root

    required property QtObject scene

    LayerStack {
        anchors.fill: parent
        scene: root.scene
        include: ["slide", "props"]
        opacity: 1.0 - root.scene.blackLevel
    }
}
