import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts

// Props (batch 14): lower thirds and logos ("bugs") that stay up while the
// slides change. Click one to put it up or take it down; one lower third and
// one bug can be up together. Clear Props (F3) takes both down.
Rectangle {
    id: root

    required property QtObject props
    signal closeRequested()

    property string bugCorner: "topRight"

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
                    text: qsTr("PROPS")
                }
                Small {
                    text: "✕"
                    variant: "ghost"
                    onClicked: root.closeRequested()
                }
            }
            LText {
                visible: root.props.props.count === 0
                text: qsTr("No props yet. Add a lower third or a corner picture below.")
                font.pixelSize: 11
                color: Theme.textMuted
            }

            Repeater {
                model: root.props.props
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property string name
                    required property string kind
                    required property string summary
                    required property bool shown
                    objectName: "propRow-" + index
                    Layout.fillWidth: true
                    implicitHeight: 48
                    radius: 8
                    color: shown ? Theme.selected : rowMouse.containsMouse ? Theme.raised : "transparent"
                    border.color: shown ? Theme.live : "transparent"

                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.props.toggle(row.index)
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        spacing: 8
                        SectionLabel {
                            Layout.preferredWidth: 44
                            text: row.kind === "bug" ? qsTr("CORNER") : qsTr("LOWER")
                            font.pixelSize: 10
                        }
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
                                visible: text !== ""
                                text: row.summary
                                font.pixelSize: 11
                                color: Theme.textMuted
                            }
                        }
                        LText {
                            visible: row.shown
                            text: qsTr("ON AIR")
                            font.pixelSize: 10
                            font.weight: Font.Bold
                            color: Theme.live
                        }
                        RemoveButton {
                            objectName: "propRemove-" + row.index
                            onConfirmed: root.props.remove(row.index)
                        }
                    }
                }
            }

            SectionLabel {
                Layout.topMargin: 6
                text: qsTr("ADD A LOWER THIRD")
            }
            SearchField {
                id: line1
                objectName: "propLine1"
                inputName: "propLine1Text"
                Layout.fillWidth: true
                placeholder: qsTr("Name, e.g. Pastor Ade Johnson")
                onAccepted: line2.focusInput() // Enter: on to the title
            }
            RowLayout {
                Layout.fillWidth: true
                SearchField {
                    id: line2
                    objectName: "propLine2"
                    inputName: "propLine2Text"
                    Layout.fillWidth: true
                    placeholder: qsTr("Title, e.g. Lead pastor")
                    onAccepted: addLower.clicked()
                }
                Small {
                    id: addLower
                    objectName: "propAddLowerThird"
                    text: qsTr("Add")
                    variant: "primary"
                    onClicked: {
                        if (root.props.addLowerThird(line1.text, line2.text)) {
                            line1.text = ""
                            line2.text = ""
                        }
                    }
                }
            }
            LText {
                objectName: "propsProblem"
                Layout.fillWidth: true
                visible: root.props.message !== ""
                text: root.props.message
                color: Theme.warnText
                font.pixelSize: 11
            }

            SectionLabel {
                Layout.topMargin: 6
                // Not the full-screen Logo (F6): a small picture in a corner.
                text: qsTr("ADD A CORNER PICTURE")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: [
                        { corner: "topLeft", label: "↖" },
                        { corner: "topRight", label: "↗" },
                        { corner: "bottomLeft", label: "↙" },
                        { corner: "bottomRight", label: "↘" }
                    ]
                    delegate: Small {
                        required property var modelData
                        objectName: "propCorner-" + modelData.corner
                        text: modelData.label
                        variant: root.bugCorner === modelData.corner ? "primary" : "default"
                        onClicked: root.bugCorner = modelData.corner
                    }
                }
                Item { Layout.fillWidth: true }
                Small {
                    objectName: "propAddBug"
                    text: qsTr("Choose picture…")
                    onClicked: bugDialog.active = true
                }
            }
        }
    }

    Loader {
        id: bugDialog
        active: false
        sourceComponent: FileDialog {
            title: qsTr("Choose a corner picture")
            nameFilters: [qsTr("Pictures (*.png *.svg *.jpg *.jpeg *.webp)")]
            Component.onCompleted: open()
            onAccepted: {
                root.props.addBug(selectedFile, root.bugCorner)
                bugDialog.active = false
            }
            onRejected: bugDialog.active = false
        }
    }
}
