import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts

// Timecode cues (batch 20): the selected item's slides following a backing
// track. Choose the track, Record (send each slide live at its moment while
// the track plays here), nudge cues a frame at a time, then Play on the
// output: the render process plays the track and changes slides on the
// frame.
Rectangle {
    id: root

    required property QtObject cues
    signal closeRequested()

    width: 460
    height: Math.min(column.implicitHeight + 24, 720)
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
                    text: qsTr("CUES · %1").arg(root.cues.itemTitle.toUpperCase())
                }
                Small {
                    text: "✕"
                    variant: "ghost"
                    onClicked: root.closeRequested()
                }
            }

            // What keeps time: a backing track played here, or MIDI
            // timecode from another device.
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                LText {
                    Layout.fillWidth: true
                    text: qsTr("Follow")
                    font.pixelSize: 12
                    color: Theme.textMuted
                }
                Small {
                    objectName: "cuesFollowTrack"
                    text: qsTr("Backing track")
                    variant: root.cues.followTimecode ? "default" : "primary"
                    onClicked: root.cues.followTimecode = false
                }
                Small {
                    objectName: "cuesFollowTimecode"
                    visible: root.cues.timecodeAvailable
                    text: qsTr("MIDI timecode")
                    variant: root.cues.followTimecode ? "primary" : "default"
                    onClicked: root.cues.followTimecode = true
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: !root.cues.followTimecode
                LText {
                    Layout.fillWidth: true
                    text: root.cues.trackName !== "" ? qsTr("Backing track: %1").arg(root.cues.trackName)
                                                    : qsTr("No backing track (cues follow the clock)")
                    elide: Text.ElideMiddle
                    font.pixelSize: 12
                }
                Small {
                    objectName: "cuesTrack"
                    text: qsTr("Choose track…")
                    onClicked: trackDialog.active = true
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: !root.cues.followTimecode
                LText {
                    Layout.fillWidth: true
                    text: qsTr("Sound delay (ms), for a mixer or wireless that makes the audience hear the track late")
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                    font.pixelSize: 11
                    color: Theme.textMuted
                }
                SearchField {
                    objectName: "cuesSoundDelay"
                    inputName: "cuesSoundDelayInput"
                    Layout.preferredWidth: 70
                    placeholder: "0"
                    text: String(root.cues.soundDelayMs)
                    onAccepted: root.cues.soundDelayMs = parseInt(text) || 0
                    onInputFocusedChanged: if (!inputFocused) root.cues.soundDelayMs = parseInt(text) || 0
                }
            }

            GridLayout {
                Layout.fillWidth: true
                visible: root.cues.followTimecode
                columns: 2
                columnSpacing: 8
                rowSpacing: 4
                LText {
                    text: qsTr("Device")
                    font.pixelSize: 12
                }
                SearchField {
                    id: deviceField
                    objectName: "cuesTimecodeDevice"
                    inputName: "cuesTimecodeDeviceInput"
                    Layout.fillWidth: true
                    placeholder: qsTr("Any MIDI input (or part of its name)")
                    text: root.cues.timecodeDevice
                    onAccepted: root.cues.timecodeDevice = text
                    onInputFocusedChanged: if (!inputFocused) root.cues.timecodeDevice = text
                }
                LText {
                    text: qsTr("Cue 0:00 at")
                    font.pixelSize: 12
                }
                SearchField {
                    id: startField
                    objectName: "cuesTimecodeStart"
                    inputName: "cuesTimecodeStartInput"
                    Layout.fillWidth: true
                    placeholder: "01:00:00:00"
                    text: root.cues.timecodeStart
                    onAccepted: root.cues.timecodeStart = text
                    onInputFocusedChanged: if (!inputFocused) root.cues.timecodeStart = text
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Small {
                    objectName: "cuesRecord"
                    text: root.cues.recording ? qsTr("Stop recording") : qsTr("Record")
                    variant: root.cues.recording ? "warn" : "default"
                    onClicked: root.cues.recording ? root.cues.stopRecording() : root.cues.startRecording()
                }
                LText {
                    visible: root.cues.recording
                    text: root.cues.position
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    font.features: { "tnum": 1 }
                    color: Theme.live
                }
                Item { Layout.fillWidth: true }
                Small {
                    objectName: "cuesPlay"
                    text: qsTr("Play on output")
                    variant: "primary"
                    visible: !root.cues.playing
                    onClicked: root.cues.play()
                }
                Small {
                    objectName: "cuesStop"
                    text: qsTr("Stop")
                    visible: root.cues.playing
                    onClicked: root.cues.stop()
                }
            }
            LText {
                Layout.fillWidth: true
                visible: text !== ""
                text: root.cues.message
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                color: Theme.live
            }

            Repeater {
                model: root.cues.cues
                delegate: RowLayout {
                    id: cue
                    required property int index
                    required property string time
                    required property string label
                    required property string text
                    objectName: "cue-" + index
                    Layout.fillWidth: true
                    spacing: 6
                    LText {
                        Layout.preferredWidth: 70
                        text: cue.time
                        font.features: { "tnum": 1 }
                        font.weight: Font.DemiBold
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        LText {
                            Layout.fillWidth: true
                            text: cue.label
                            font.pixelSize: 11
                            color: Theme.textMuted
                        }
                        LText {
                            Layout.fillWidth: true
                            text: cue.text
                            font.pixelSize: 12
                        }
                    }
                    Small {
                        objectName: "cueEarlier-" + cue.index
                        text: "−1f"
                        onClicked: root.cues.nudge(cue.index, -1)
                    }
                    Small {
                        objectName: "cueLater-" + cue.index
                        text: "+1f"
                        onClicked: root.cues.nudge(cue.index, 1)
                    }
                    Small {
                        text: "🗑"
                        variant: "ghost"
                        onClicked: root.cues.removeCue(cue.index)
                    }
                }
            }
            LText {
                Layout.fillWidth: true
                visible: root.cues.cues.count === 0 && !root.cues.recording
                text: qsTr("No cues yet. Press Record, then send each slide live (click it, or Space) when its moment comes in the track.")
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                color: Theme.textMuted
            }
        }
    }

    Loader {
        id: trackDialog
        active: false
        sourceComponent: FileDialog {
            title: qsTr("Choose a backing track")
            nameFilters: [qsTr("Audio (*.mp3 *.m4a *.aac *.wav *.flac *.ogg *.opus)"), qsTr("All files (*)")]
            Component.onCompleted: open()
            onAccepted: {
                root.cues.setTrack(selectedFile)
                trackDialog.active = false
            }
            onRejected: trackDialog.active = false
        }
    }
}
