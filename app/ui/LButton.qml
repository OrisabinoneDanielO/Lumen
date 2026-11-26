import QtQuick

// Button styled as in the mockups. Batch 1 buttons have no actions yet.
Rectangle {
    id: root

    property string text
    property string hint            // keyboard shortcut shown after the label, e.g. "F1"
    property bool showHint: true    // false: only named (screen readers), not shown
    property string variant: "default" // default | warn | black | primary | ghost
    property int fontSize: 12
    signal clicked

    implicitWidth: label.implicitWidth + 24
    implicitHeight: 36
    radius: 8
    color: {
        switch (variant) {
        case "warn": return Theme.warnBackground;
        case "black": return "#000000";
        case "primary": return Theme.live;
        case "ghost": return "transparent";
        default: return Theme.raised;
        }
    }
    border.width: variant === "primary" ? 0 : 1
    border.color: variant === "warn" ? Theme.warnBorder : Theme.border

    Accessible.role: Accessible.Button
    Accessible.name: hint !== "" ? text + " (" + hint + ")" : text

    Row {
        id: label
        anchors.centerIn: parent
        spacing: 5

        LText {
            text: root.text
            font.pixelSize: root.fontSize
            font.weight: root.variant === "primary" ? Font.Bold
                       : root.variant === "warn" ? Font.DemiBold : Font.Normal
            color: root.variant === "primary" ? Theme.window
                 : root.variant === "warn" ? Theme.warnText : Theme.text
        }
        LText {
            visible: root.hint !== "" && root.showHint
            text: root.hint
            font.pixelSize: root.fontSize
            color: root.variant === "warn" ? Theme.warnHint : Theme.textMuted
        }
    }

    // Feedback: lighter while pointed at, darker while pressed, faded when
    // it cannot be used, so every click visibly lands.
    opacity: enabled ? 1 : 0.45
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: buttonArea.pressed ? "#40000000" : buttonArea.containsMouse ? "#1FFFFFFF" : "transparent"
    }

    MouseArea {
        id: buttonArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
