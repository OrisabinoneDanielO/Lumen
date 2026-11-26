import QtQuick
import QtQuick.Layouts

// Remote control (batch 19): the phone and tablet remote (a web page on the
// local network, with a code), OSC, and MIDI controllers with learnable
// buttons. Everything is off until turned on here.
Rectangle {
    id: root

    required property QtObject remote
    required property QtObject osc
    required property QtObject midi
    signal closeRequested()

    width: 460
    height: Math.min(column.implicitHeight + 24, 760)
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
                    text: qsTr("REMOTE CONTROL")
                }
                Small {
                    text: "✕"
                    variant: "ghost"
                    onClicked: root.closeRequested()
                }
            }

            // ---- phone and tablet ----
            RowLayout {
                Layout.fillWidth: true
                LText {
                    Layout.fillWidth: true
                    text: qsTr("Phone and tablet remote")
                    font.weight: Font.Medium
                }
                Small {
                    objectName: "remoteToggle"
                    text: root.remote.enabled ? qsTr("Turn off") : qsTr("Turn on")
                    variant: root.remote.enabled ? "default" : "primary"
                    onClicked: root.remote.enabled = !root.remote.enabled
                }
            }
            Rectangle {
                visible: root.remote.enabled
                Layout.fillWidth: true
                implicitHeight: remoteInfo.implicitHeight + 20
                radius: 8
                color: Theme.raised
                ColumnLayout {
                    id: remoteInfo
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 4
                    LText {
                        text: qsTr("On a phone on this network, open:")
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                    Repeater {
                        model: root.remote.addresses
                        delegate: LText {
                            required property string modelData
                            objectName: "remoteAddress"
                            text: modelData
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                    }
                    LText {
                        text: qsTr("and enter the code")
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                    LText {
                        objectName: "remoteCode"
                        text: root.remote.code
                        font.pixelSize: 28
                        font.weight: Font.Bold
                        font.letterSpacing: 4
                        font.features: { "tnum": 1 }
                    }
                }
            }

            // ---- OSC ----
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    LText {
                        text: qsTr("OSC")
                        font.weight: Font.Medium
                    }
                    LText {
                        Layout.fillWidth: true
                        text: root.osc.enabled ? root.osc.status
                                               : qsTr("For show control and tablet apps: /lumen/next, /lumen/previous, /lumen/slide 3, …")
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                }
                Small {
                    objectName: "oscToggle"
                    text: root.osc.enabled ? qsTr("Turn off") : qsTr("Turn on")
                    variant: root.osc.enabled ? "default" : "primary"
                    onClicked: root.osc.enabled = !root.osc.enabled
                }
            }

            // ---- MIDI ----
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    LText {
                        text: qsTr("MIDI controllers")
                        font.weight: Font.Medium
                    }
                    LText {
                        Layout.fillWidth: true
                        text: !root.midi.available ? qsTr("Not available in this build")
                              : !root.midi.enabled ? qsTr("Pads, keyboards and foot switches drive the slides")
                              : root.midi.devices.length === 0 ? qsTr("No MIDI device found yet; plug one in")
                              : qsTr("Listening to: %1").arg(root.midi.devices.join(", "))
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                }
                Small {
                    objectName: "midiToggle"
                    visible: root.midi.available
                    text: root.midi.enabled ? qsTr("Turn off") : qsTr("Turn on")
                    variant: root.midi.enabled ? "default" : "primary"
                    onClicked: root.midi.enabled = !root.midi.enabled
                }
            }
            LText {
                visible: root.midi.enabled && root.midi.last !== ""
                text: qsTr("Last: %1").arg(root.midi.last)
                font.pixelSize: 11
                color: Theme.live
            }
            Repeater {
                model: root.midi.enabled ? root.midi.mappings : null
                delegate: RowLayout {
                    id: mapping
                    required property int index
                    required property string action
                    required property string label
                    required property string message
                    Layout.fillWidth: true
                    LText {
                        Layout.fillWidth: true
                        text: mapping.label
                        font.pixelSize: 12
                    }
                    LText {
                        text: root.midi.learning === mapping.action ? qsTr("Press a key or pad…") : mapping.message
                        font.pixelSize: 12
                        color: root.midi.learning === mapping.action ? Theme.live : Theme.textMuted
                    }
                    Small {
                        objectName: "midiLearn-" + mapping.action
                        text: root.midi.learning === mapping.action ? qsTr("Cancel") : qsTr("Learn")
                        onClicked: root.midi.learn(root.midi.learning === mapping.action ? "" : mapping.action)
                    }
                }
            }
            Small {
                visible: root.midi.enabled
                text: qsTr("Back to the default notes")
                onClicked: root.midi.resetMappings()
            }
        }
    }
}
