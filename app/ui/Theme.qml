pragma Singleton
import QtQuick

// Operator UI colours, taken from design/mockups/Main.html.
QtObject {
    readonly property color window: "#0F1115"
    readonly property color panel: "#171A20"
    readonly property color raised: "#1E222A"
    readonly property color border: "#2B303A"
    readonly property color text: "#E9ECF1"
    readonly property color textMuted: "#A3ACBA"
    readonly property color accent: "#F08A24"
    readonly property color ok: "#3DCB8A"

    readonly property int toolbarHeight: 56
    readonly property int planWidth: 240
    readonly property int previewWidth: 360
    readonly property int libraryHeight: 220
}
