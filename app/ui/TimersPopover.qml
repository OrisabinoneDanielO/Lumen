import QtQuick
import QtQuick.Layouts

// Timers (batch 12): start, pause and reset each timer, show one in the
// Messages banner, and add or remove timers. The output counts on its own
// clock, so what is shown here and there agree without the UI driving it.
Rectangle {
    id: root

    required property QtObject timers
    required property QtObject controller
    signal closeRequested()

    property string newMode: "countdown"

    width: 420
    height: Math.min(column.implicitHeight + 24, 640)
    radius: 10
    color: Theme.window
    border.color: Theme.border

    component Small: LButton {
        implicitHeight: 28
        radius: 6
        fontSize: 11
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 12
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: column
            width: parent.width
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                SectionLabel {
                    Layout.fillWidth: true
                    text: qsTr("TIMERS")
                }
                Small {
                    objectName: "timerHide"
                    text: qsTr("Off the stage")
                    onClicked: root.timers.hide()
                }
                Small {
                    text: "✕"
                    variant: "ghost"
                    onClicked: root.closeRequested()
                }
            }

            Repeater {
                model: root.timers.timers
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property string name
                    required property string mode
                    required property string setting
                    required property string display
                    required property bool running
                    required property bool shown
                    objectName: "timerRow-" + index
                    Layout.fillWidth: true
                    implicitHeight: 64
                    radius: 8
                    color: shown ? Theme.selected : Theme.raised

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            LText {
                                Layout.fillWidth: true
                                text: row.name
                                font.weight: Font.Medium
                            }
                            LText {
                                Layout.fillWidth: true
                                text: row.setting
                                font.pixelSize: 11
                                color: Theme.textMuted
                            }
                        }
                        LText {
                            objectName: "timerDisplay-" + row.index
                            text: row.display
                            font.pixelSize: 22
                            font.weight: Font.DemiBold
                            font.features: { "tnum": 1 }
                            color: row.running ? Theme.text : Theme.textMuted
                        }
                        Small {
                            objectName: "timerStart-" + row.index
                            visible: row.mode !== "countdownToTime"
                            text: row.running ? qsTr("Pause") : qsTr("Start")
                            variant: row.running ? "default" : "primary"
                            onClicked: row.running ? root.timers.pause(row.index) : root.timers.start(row.index)
                        }
                        Small {
                            objectName: "timerReset-" + row.index
                            visible: row.mode !== "countdownToTime"
                            text: qsTr("Reset")
                            onClicked: root.timers.reset(row.index)
                        }
                        Small {
                            objectName: "timerShow-" + row.index
                            // On the stage display, for the speaker; the
                            // audience never sees timers (countdown slides
                            // excepted).
                            text: qsTr("On stage")
                            variant: row.shown ? "primary" : "default"
                            onClicked: row.shown ? root.timers.hide() : root.timers.show(row.index)
                        }
                        RemoveButton {
                            objectName: "timerRemove-" + row.index
                            onConfirmed: root.timers.remove(row.index)
                        }
                    }
                }
            }

            // A new timer.
            SectionLabel {
                Layout.topMargin: 6
                text: qsTr("ADD A TIMER")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: [
                        { mode: "countdown", label: qsTr("Countdown") },
                        { mode: "countdownToTime", label: qsTr("Until a time") },
                        { mode: "elapsed", label: qsTr("Count up") }
                    ]
                    delegate: Small {
                        required property var modelData
                        objectName: "timerMode-" + modelData.mode
                        text: modelData.label
                        variant: root.newMode === modelData.mode ? "primary" : "default"
                        onClicked: root.newMode = modelData.mode
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                SearchField {
                    id: nameField
                    objectName: "timerName"
                    inputName: "timerNameText"
                    Layout.fillWidth: true
                    placeholder: qsTr("Name, e.g. Announcements")
                    // Enter: on to the time, or (count up) add it.
                    onAccepted: valueField.visible ? valueField.focusInput() : addButton.clicked()
                }
                SearchField {
                    id: valueField
                    objectName: "timerValue"
                    inputName: "timerValueText"
                    visible: root.newMode !== "elapsed"
                    Layout.preferredWidth: 110
                    placeholder: root.newMode === "countdown" ? qsTr("Minutes") : qsTr("e.g. 10:30")
                    onAccepted: addButton.clicked()
                }
                Small {
                    id: addButton
                    objectName: "timerAdd"
                    text: qsTr("Add")
                    variant: "primary"
                    onClicked: {
                        if (root.timers.add(nameField.text, root.newMode, valueField.text)) {
                            nameField.text = ""
                            valueField.text = ""
                            addError.visible = false
                        } else {
                            addError.visible = true
                        }
                    }
                }
            }
            LText {
                id: addError
                visible: false
                text: root.newMode === "countdown" ? qsTr("Give a name and the number of minutes.")
                                                   : qsTr("Give a name and a time such as 10:30.")
                font.pixelSize: 11
                color: Theme.live
            }
        }
    }
}
