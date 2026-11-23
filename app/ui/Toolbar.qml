import QtQuick
import QtQuick.Layouts

// Top toolbar: logo, Show/Edit switch, plan name, Clear buttons, Logo, Black
// and output status lights.
Rectangle {
    id: root

    property string planName
    property string planDetail
    property bool outputsConnected: false

    implicitHeight: 58
    color: Theme.panel

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
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
                text: "Lumen"
                font.pixelSize: 16
                font.bold: true
            }
        }

        // Show / Edit mode switch (Show is the default mode).
        Rectangle {
            implicitWidth: modes.implicitWidth + 8
            implicitHeight: modes.implicitHeight + 8
            radius: 8
            color: Theme.window
            border.color: Theme.border

            Row {
                id: modes
                anchors.centerIn: parent
                spacing: 2

                Rectangle {
                    width: showLabel.implicitWidth + 28
                    height: 30
                    radius: 6
                    color: Theme.border
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: qsTr("Show mode")
                    LText {
                        id: showLabel
                        anchors.centerIn: parent
                        text: qsTr("Show")
                        font.weight: Font.DemiBold
                    }
                }
                Rectangle {
                    width: editLabel.implicitWidth + 28
                    height: 30
                    radius: 6
                    color: "transparent"
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: qsTr("Edit mode")
                    LText {
                        id: editLabel
                        anchors.centerIn: parent
                        text: qsTr("Edit")
                        color: Theme.textMuted
                    }
                }
            }
        }

        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true
            Layout.minimumWidth: 80

            LText {
                Layout.fillWidth: true
                text: root.planName
                font.weight: Font.DemiBold
            }
            LText {
                Layout.fillWidth: true
                text: root.planDetail
                font.pixelSize: 12
                color: Theme.textMuted
            }
        }

        Row {
            spacing: 6
            LButton { text: qsTr("Clear BG"); hint: "F1" }
            LButton { text: qsTr("Clear Slide"); hint: "F2" }
            LButton { text: qsTr("Clear Props"); hint: "F3" }
            LButton { text: qsTr("Clear Msg"); hint: "F4" }
            LButton { text: qsTr("Clear All"); hint: "F5"; variant: "warn" }
            LButton { text: qsTr("Logo"); hint: "F6" }
            LButton { text: qsTr("Black"); hint: "F7"; variant: "black" }
        }

        Row {
            spacing: 12
            visible: root.width >= 1360

            Repeater {
                model: [qsTr("Main"), qsTr("Stage"), qsTr("Stream (NDI)")]

                delegate: Row {
                    id: output
                    required property string modelData
                    spacing: 6
                    Accessible.role: Accessible.StaticText
                    Accessible.name: modelData + (root.outputsConnected ? qsTr(" connected")
                                                                        : qsTr(" not connected"))

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 8
                        radius: 4
                        color: root.outputsConnected ? Theme.ok : Theme.unknown
                    }
                    LText {
                        text: output.modelData
                        font.pixelSize: 12
                        color: Theme.textMuted
                    }
                }
            }
        }
    }
}
