import QtQuick
import QtQuick.Layouts

// The screen setup: every connected screen shows its number in large type,
// and here the operator says which one is the main output, which the stage
// display, and which shows nothing. Opens by itself the first time a set of
// screens is connected; Plan ▾ → Screens… opens it later.
Rectangle {
    id: root

    required property QtObject setup
    signal closeRequested()

    width: 520
    height: column.implicitHeight + 24
    radius: 10
    color: Theme.window
    border.color: Theme.border

    // Takes the keyboard while open: Escape or Return closes it, and no other
    // key reaches the live shortcuts.
    focus: true
    Component.onCompleted: forceActiveFocus()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
            root.closeRequested()
        event.accepted = true
    }

    function roleColour(role) {
        return role === "main" ? Theme.live : role === "stage" ? Theme.next : role === "operator" ? Theme.ok : Theme.unknown
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            SectionLabel {
                Layout.fillWidth: true
                text: qsTr("SCREENS")
            }
            LButton {
                text: "✕"
                variant: "ghost"
                implicitHeight: 28
                onClicked: root.closeRequested()
            }
        }
        LText {
            Layout.fillWidth: true
            text: qsTr("Each screen now shows its number. Choose what each one shows; the outputs move as you choose, and Lumen remembers it for these screens.")
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            font.pixelSize: 11
            color: Theme.textMuted
        }

        // The arrangement, as in the system's display settings.
        Item {
            id: arrangement
            Layout.fillWidth: true
            Layout.preferredHeight: 130
            // As large as fits, centred.
            readonly property real scale: Math.min(width / Math.max(0.01, root.setup.spanX),
                                                   height / Math.max(0.01, root.setup.spanY))
            readonly property real offsetX: (width - root.setup.spanX * scale) / 2
            readonly property real offsetY: (height - root.setup.spanY * scale) / 2
            Repeater {
                model: root.setup.screens
                delegate: Rectangle {
                    required property int number
                    required property string role
                    required property real areaX
                    required property real areaY
                    required property real areaW
                    required property real areaH
                    objectName: "screenTile-" + number
                    x: arrangement.offsetX + areaX * arrangement.scale + 2
                    y: arrangement.offsetY + areaY * arrangement.scale + 2
                    width: Math.max(24, areaW * arrangement.scale - 4)
                    height: Math.max(16, areaH * arrangement.scale - 4)
                    radius: 4
                    color: Theme.raised
                    border.color: root.roleColour(role)
                    border.width: 2
                    LText {
                        anchors.centerIn: parent
                        text: parent.number
                        font.pixelSize: Math.max(12, parent.height * 0.45)
                        font.weight: Font.Bold
                        color: root.roleColour(parent.role)
                    }
                }
            }
        }

        Repeater {
            model: root.setup.screens
            delegate: Rectangle {
                id: row
                required property int index
                required property int number
                required property string name
                required property string size
                required property string role
                required property bool operator
                objectName: "screenRow-" + number
                Layout.fillWidth: true
                implicitHeight: 52
                radius: 6
                color: Theme.panel
                border.color: Theme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 8
                    spacing: 10
                    LText {
                        text: row.number
                        font.pixelSize: 22
                        font.weight: Font.Bold
                        color: root.roleColour(row.role)
                        Layout.preferredWidth: 24
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        LText {
                            Layout.fillWidth: true
                            text: row.name
                            font.weight: Font.Medium
                            font.pixelSize: 12
                        }
                        LText {
                            Layout.fillWidth: true
                            text: row.operator ? qsTr("%1 · this screen (the operator window)").arg(row.size) : row.size
                            font.pixelSize: 11
                            color: Theme.textMuted
                        }
                    }
                    Row {
                        visible: !row.operator
                        spacing: 4
                        Repeater {
                            model: [
                                { key: "main", label: qsTr("Main output") },
                                { key: "stage", label: qsTr("Stage") },
                                { key: "none", label: qsTr("Not used") }
                            ]
                            delegate: LButton {
                                required property var modelData
                                objectName: "screenRole-" + row.number + "-" + modelData.key
                                text: modelData.label
                                implicitHeight: 28
                                radius: 6
                                variant: row.role === modelData.key ? "primary" : "default"
                                onClicked: root.setup.setRole(row.index, modelData.key)
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            LButton {
                objectName: "screensDone"
                text: qsTr("Done")
                variant: "primary"
                implicitHeight: 30
                radius: 6
                onClicked: root.closeRequested()
            }
        }
    }
}
