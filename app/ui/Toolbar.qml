import QtQuick
import QtQuick.Layouts

// Top toolbar: logo, Show/Edit switch, plan name, Timers and Message
// (batch 12), Clear buttons, Logo, Black and output status lights (click
// Main to turn the output on or off).
Rectangle {
    id: root

    required property QtObject controller
    // The running timer, e.g. "Sermon 24:13" (empty when none runs).
    property string timerSummary
    signal timersRequested()
    signal messageRequested()
    signal stageRequested()
    signal propsRequested()
    signal streamRequested()
    property bool streamConnected: false
    // Whether the stage display is up (batch 13).
    property bool stageConnected: false

    implicitHeight: 58
    color: Theme.panel

    // Narrower windows: first the F-key hints go (the keys still work and
    // are named for screen readers), then the Clear buttons shorten to
    // "Clear: BG Slide Props Msg All" and the name next to the logo goes.
    // Every button and the output lights stay on screen.
    readonly property bool compact: width < 1500
    readonly property bool narrow: width < 1250

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    component ModeButton: Rectangle {
        id: modeButton
        property string label
        property string mode
        readonly property bool active: root.controller.mode === mode

        width: modeLabel.implicitWidth + 28
        height: 30
        radius: 6
        color: active ? Theme.border : "transparent"
        Accessible.role: Accessible.RadioButton
        Accessible.name: label
        Accessible.checked: active

        LText {
            id: modeLabel
            anchors.centerIn: parent
            text: modeButton.label
            font.weight: modeButton.active ? Font.DemiBold : Font.Normal
            color: modeButton.active ? Theme.text : Theme.textMuted
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.controller.mode = modeButton.mode
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 16

        Row {
            spacing: 8
            LumenLogo {
                anchors.verticalCenter: parent.verticalCenter
            }
            LText {
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.narrow
                text: "Lumen"
                font.pixelSize: 16
                font.bold: true
            }
        }

        // Show / Edit mode switch. Edit stays hidden until editing works
        // (experimental/editor setting).
        Rectangle {
            objectName: "modeSwitch"
            implicitWidth: modes.implicitWidth + 8
            implicitHeight: modes.implicitHeight + 8
            radius: 8
            color: Theme.window
            border.color: Theme.border

            Row {
                id: modes
                anchors.centerIn: parent
                spacing: 2

                ModeButton {
                    objectName: "showMode"
                    label: qsTr("Show")
                    mode: "show"
                }
                ModeButton {
                    objectName: "editMode"
                    label: qsTr("Edit")
                    mode: "edit"
                    visible: root.controller.editAvailable
                }
            }
        }

        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true
            Layout.minimumWidth: root.narrow ? 40 : 80

            LText {
                Layout.fillWidth: true
                text: root.controller.planName
                font.weight: Font.DemiBold
            }
            LText {
                Layout.fillWidth: true
                text: root.controller.planDetail
                font.pixelSize: 12
                color: Theme.textMuted
            }
        }

        Row {
            spacing: 6
            LButton {
                objectName: "timersButton"
                text: root.timerSummary !== "" ? root.timerSummary : qsTr("Timers")
                onClicked: root.timersRequested()
            }
            LButton {
                objectName: "propsButton"
                text: qsTr("Props")
                onClicked: root.propsRequested()
            }
            LButton {
                objectName: "messageButton"
                text: qsTr("Message")
                variant: root.controller.messageShown !== "" ? "primary" : "default"
                onClicked: root.messageRequested()
            }
            Item { width: 6; height: 1 }
            LText {
                visible: root.narrow
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Clear")
                font.pixelSize: 12
                color: Theme.textMuted
            }
            LButton {
                objectName: "clearBackground"
                text: root.narrow ? qsTr("BG") : qsTr("Clear BG"); hint: "F1"; showHint: !root.compact
                onClicked: root.controller.clearLayer("background")
            }
            LButton {
                objectName: "clearSlide"
                text: root.narrow ? qsTr("Slide") : qsTr("Clear Slide"); hint: "F2"; showHint: !root.compact
                onClicked: root.controller.clearLayer("slide")
            }
            LButton {
                objectName: "clearProps"
                text: root.narrow ? qsTr("Props") : qsTr("Clear Props"); hint: "F3"; showHint: !root.compact
                onClicked: root.controller.clearLayer("props")
            }
            LButton {
                objectName: "clearMessages"
                text: root.narrow ? qsTr("Msg") : qsTr("Clear Msg"); hint: "F4"; showHint: !root.compact
                onClicked: root.controller.clearLayer("messages")
            }
            LButton {
                objectName: "clearAll"
                text: root.narrow ? qsTr("All") : qsTr("Clear All"); hint: "F5"; showHint: !root.compact; variant: "warn"
                onClicked: root.controller.clearAll()
            }
            LButton {
                objectName: "logo"
                text: qsTr("Logo"); hint: "F6"; showHint: !root.compact
                variant: root.controller.logoOn ? "primary" : "default"
                onClicked: root.controller.toggleLogo()
            }
            LButton {
                objectName: "black"
                text: qsTr("Black"); hint: "F7"; showHint: !root.compact
                variant: root.controller.blackOn ? "primary" : "black"
                onClicked: root.controller.toggleBlack()
            }
        }

        // Main, Stage (batch 13) and Stream (batch 15): click Main to turn
        // the output off or on, Stage or Stream for their settings.
        Row {
            spacing: root.narrow ? 8 : 12

            Repeater {
                model: [
                    { name: qsTr("Main"), connected: root.controller.outputConnected },
                    { name: qsTr("Stage"), connected: root.stageConnected },
                    { name: qsTr("Stream"), connected: root.streamConnected }
                ]

                delegate: Row {
                    id: output
                    required property var modelData
                    required property int index
                    objectName: "output-" + index
                    spacing: 6
                    Accessible.role: Accessible.StaticText
                    Accessible.name: modelData.name + (modelData.connected ? qsTr(" connected")
                                                                           : qsTr(" not connected"))

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 8
                        radius: 4
                        color: output.modelData.connected ? Theme.ok : Theme.unknown
                    }
                    LText {
                        text: output.modelData.name
                        font.pixelSize: 12
                        color: Theme.textMuted
                    }
                    // Main turns the output on and off; Stage opens the stage
                    // display's settings.
                    TapHandler {
                        onTapped: output.index === 0 ? root.controller.toggleOutput()
                                  : output.index === 1 ? root.stageRequested() : root.streamRequested()
                    }
                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }
                }
            }
        }
    }
}
