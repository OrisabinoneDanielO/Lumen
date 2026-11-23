pragma Singleton
import QtQuick

// Operator UI colours and sizes, taken from design/mockups/Main.html.
QtObject {
    readonly property string fontFamily: "Inter"

    readonly property color window: "#0F1115"
    readonly property color panel: "#171A20"
    readonly property color raised: "#1E222A"
    readonly property color border: "#2B303A"
    readonly property color selected: "#252B36"
    readonly property color rowSelected: "#22304A"
    readonly property color text: "#E9ECF1"
    readonly property color textMuted: "#A3ACBA"
    readonly property color link: "#8DB4FF"

    readonly property color live: "#F08A24"
    readonly property color next: "#4C8DFF"
    readonly property color slideBackground: "#14213D"
    readonly property color ok: "#3DCB8A"
    readonly property color unknown: "#5A6170"

    readonly property color warnBackground: "#3A2A1E"
    readonly property color warnBorder: "#6B4523"
    readonly property color warnText: "#FFD8B0"
    readonly property color warnHint: "#E8B98A"

    readonly property int planWidth: 260
    readonly property int previewWidth: 360
    readonly property int libraryHeight: 230
    readonly property int slideMinWidth: 200
}
