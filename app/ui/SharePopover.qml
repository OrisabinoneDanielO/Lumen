import QtQuick
import QtQuick.Layouts

// Sharing with other Lumen computers on the local network (batch 18): no
// internet needed. Share this computer (it shows a code), or find another,
// give its code, and copy its plans and songs or send it the open plan.
Rectangle {
    id: root

    required property QtObject sharing
    required property QtObject controller
    signal closeRequested()

    property int peer: -1
    property var chosenSongs: []

    Component.onCompleted: sharing.discover()

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
                    text: qsTr("SHARE WITH OTHER COMPUTERS")
                }
                Small {
                    text: "✕"
                    variant: "ghost"
                    onClicked: root.closeRequested()
                }
            }

            // This computer.
            RowLayout {
                Layout.fillWidth: true
                LText {
                    Layout.fillWidth: true
                    text: root.sharing.enabled ? qsTr("This computer, \"%1\", is shared.").arg(root.sharing.name)
                                               : qsTr("This computer is not shared.")
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                }
                Small {
                    objectName: "shareToggle"
                    text: root.sharing.enabled ? qsTr("Stop sharing") : qsTr("Share this computer")
                    variant: root.sharing.enabled ? "default" : "primary"
                    onClicked: root.sharing.enabled = !root.sharing.enabled
                }
            }
            Rectangle {
                visible: root.sharing.enabled
                Layout.fillWidth: true
                implicitHeight: codeColumn.implicitHeight + 20
                radius: 8
                color: Theme.raised
                ColumnLayout {
                    id: codeColumn
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 2
                    LText {
                        text: qsTr("Code for the other computer")
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                    LText {
                        objectName: "shareCode"
                        text: root.sharing.code
                        font.pixelSize: 28
                        font.weight: Font.Bold
                        font.features: { "tnum": 1 }
                        font.letterSpacing: 4
                    }
                    LText {
                        text: qsTr("Address: %1").arg(root.sharing.address)
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                }
            }

            // Other computers.
            SectionLabel {
                Layout.topMargin: 6
                text: qsTr("OTHER COMPUTERS")
            }
            LText {
                visible: root.sharing.peers.count === 0
                Layout.fillWidth: true
                text: qsTr("Looking… Lumen computers sharing on this network appear here. Or type an address below.")
                wrapMode: Text.WordWrap
                font.pixelSize: 11
                color: Theme.textMuted
            }
            Repeater {
                model: root.sharing.peers
                delegate: Rectangle {
                    id: peerRow
                    required property int index
                    required property string name
                    required property string address
                    objectName: "sharePeer-" + index
                    Layout.fillWidth: true
                    implicitHeight: 36
                    radius: 6
                    color: root.peer === index ? Theme.selected : peerMouse.containsMouse ? Theme.raised : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        LText {
                            Layout.fillWidth: true
                            text: peerRow.name
                            font.weight: Font.Medium
                        }
                        LText {
                            text: peerRow.address
                            font.pixelSize: 11
                            color: Theme.textMuted
                        }
                    }
                    MouseArea {
                        id: peerMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.peer = peerRow.index
                            addressField.text = peerRow.address
                            codeField.focusInput()
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                SearchField {
                    id: addressField
                    objectName: "shareAddress"
                    inputName: "shareAddressText"
                    Layout.fillWidth: true
                    placeholder: qsTr("Address, e.g. 192.168.1.20:45460")
                }
                SearchField {
                    id: codeField
                    objectName: "shareRemoteCode"
                    inputName: "shareRemoteCodeText"
                    Layout.preferredWidth: 100
                    placeholder: qsTr("Code")
                    onAccepted: connectButton.clicked()
                }
                Small {
                    id: connectButton
                    objectName: "shareConnect"
                    text: qsTr("Connect")
                    variant: "primary"
                    enabled: !root.sharing.busy
                    onClicked: {
                        root.chosenSongs = []
                        root.sharing.connectToAddress(addressField.text, codeField.text)
                    }
                }
            }
            LText {
                objectName: "shareStatus"
                Layout.fillWidth: true
                visible: text !== ""
                text: root.sharing.busy ? qsTr("Working…") : root.sharing.status
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
                font.pixelSize: 11
                color: Theme.live
            }

            // What the other computer has.
            SectionLabel {
                visible: root.sharing.remotePlans.count > 0
                text: qsTr("ITS PLANS")
            }
            Repeater {
                model: root.sharing.remotePlans
                delegate: RowLayout {
                    id: planRow
                    required property int index
                    required property string name
                    required property string date
                    required property int items
                    Layout.fillWidth: true
                    LText {
                        Layout.fillWidth: true
                        text: planRow.name
                    }
                    LText {
                        text: qsTr("%1 · %2 items").arg(planRow.date).arg(planRow.items)
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                    Small {
                        objectName: "shareCopyPlan-" + planRow.index
                        text: qsTr("Copy here")
                        enabled: !root.sharing.busy
                        onClicked: root.sharing.copyPlan(planRow.index)
                    }
                }
            }
            Small {
                objectName: "shareSendPlan"
                visible: root.sharing.remotePlans.count > 0 || root.sharing.remoteSongs.count > 0
                text: qsTr("Send the open plan (%1) there").arg(root.controller.planName)
                enabled: !root.sharing.busy && root.controller.planId !== ""
                onClicked: root.sharing.sendPlan(root.controller.planId)
            }
            SectionLabel {
                visible: root.sharing.remoteSongs.count > 0
                text: qsTr("ITS SONGS")
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: root.sharing.remoteSongs
                    delegate: LButton {
                        required property int index
                        required property string title
                        objectName: "shareSong-" + index
                        text: title
                        implicitHeight: 28
                        radius: 6
                        fontSize: 11
                        variant: root.chosenSongs.indexOf(index) >= 0 ? "primary" : "default"
                        onClicked: {
                            const chosen = root.chosenSongs.slice()
                            const at = chosen.indexOf(index)
                            if (at >= 0)
                                chosen.splice(at, 1)
                            else
                                chosen.push(index)
                            root.chosenSongs = chosen
                        }
                    }
                }
            }
            Small {
                objectName: "shareCopySongs"
                visible: root.sharing.remoteSongs.count > 0
                text: root.chosenSongs.length > 0 ? qsTr("Copy %1 songs here").arg(root.chosenSongs.length)
                                                  : qsTr("Choose songs to copy")
                enabled: !root.sharing.busy && root.chosenSongs.length > 0
                onClicked: {
                    root.sharing.copySongs(root.chosenSongs)
                    root.chosenSongs = []
                }
            }
        }
    }
}
