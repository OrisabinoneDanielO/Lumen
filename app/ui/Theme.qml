pragma Singleton
import QtQuick

// Operator UI colours, sizes and font, taken from design/mockups/Main.html.
QtObject {
    // Bundled Inter (fonts/, SIL OFL 1.1). Loading any weight registers the
    // family; each loader adds one weight so Font.Medium/DemiBold/Bold are real.
    readonly property FontLoader interRegular: FontLoader { source: "fonts/Inter-Regular.ttf" }
    readonly property FontLoader interMedium: FontLoader { source: "fonts/Inter-Medium.ttf" }
    readonly property FontLoader interSemiBold: FontLoader { source: "fonts/Inter-SemiBold.ttf" }
    readonly property FontLoader interBold: FontLoader { source: "fonts/Inter-Bold.ttf" }
    readonly property string fontFamily: interRegular.status === FontLoader.Ready ? interRegular.name
                                                                                    : "sans-serif"

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

    readonly property int planWidth: 280
    readonly property int previewWidth: 360
    readonly property int libraryHeight: 230
    readonly property int slideMinWidth: 200
}
