import QtQuick
import QtQuick.Layouts

// Centre region in Show mode: the selected item's title, arrangement and
// slide cards in play order. Clicking a card sends it live.
Item {
    id: root

    required property QtObject controller
    signal slideClicked(int index)
    signal cuesRequested() // batch 20: the Cues popover
    property bool cuesAvailable: false

    Flickable {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.topMargin: 16
        clip: true
        contentHeight: content.height + 16
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: content
            width: parent.width
            spacing: 14

            RowLayout {
                width: parent.width
                visible: root.controller.itemTitle !== ""
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    LText {
                        Layout.fillWidth: true
                        text: root.controller.itemTitle
                        font.pixelSize: 22
                        font.bold: true
                    }
                    LText {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: root.controller.itemDetail
                        color: Theme.textMuted
                    }
                }

                // Announcements and the like: each slide stays up this long,
                // then the next, round and round, once it is live.
                Row {
                    objectName: "autoAdvanceRow"
                    Layout.alignment: Qt.AlignBottom
                    visible: root.controller.canAutoAdvance && !root.controller.shownIsSong
                    spacing: 4
                    LText {
                        height: 28
                        verticalAlignment: Text.AlignVCenter
                        rightPadding: 2
                        text: qsTr("Auto")
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                    Repeater {
                        model: [0, 5, 8, 10, 15, 30]
                        delegate: LButton {
                            required property int modelData
                            objectName: "autoAdvance-" + modelData
                            text: modelData === 0 ? qsTr("Off") : qsTr("%1 s").arg(modelData)
                            variant: root.controller.shownAutoAdvance === modelData ? "primary" : "default"
                            implicitHeight: 28
                            radius: 6
                            fontSize: 11
                            onClicked: root.controller.setShownAutoAdvance(modelData)
                        }
                    }
                }

                // A song: its key for the band, moved up or down a semitone
                // (the stage display's Chords layout follows; the audience
                // never sees chords).
                Row {
                    objectName: "transposeRow"
                    Layout.alignment: Qt.AlignBottom
                    visible: root.controller.shownIsSong
                    spacing: 4
                    LText {
                        objectName: "shownKey"
                        height: 28
                        verticalAlignment: Text.AlignVCenter
                        rightPadding: 4
                        text: root.controller.shownKey !== ""
                              ? qsTr("Key %1").arg(root.controller.shownKey)
                                + (root.controller.shownTranspose !== 0
                                   ? " (" + (root.controller.shownTranspose > 0 ? "+" : "") + root.controller.shownTranspose + ")" : "")
                              : root.controller.shownTranspose !== 0
                                ? qsTr("Transpose %1").arg((root.controller.shownTranspose > 0 ? "+" : "") + root.controller.shownTranspose)
                                : qsTr("Transpose")
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                    LButton {
                        objectName: "transposeDown"
                        text: "-"
                        implicitHeight: 28
                        radius: 6
                        fontSize: 12
                        Accessible.name: qsTr("Down a semitone")
                        onClicked: root.controller.transposeShown(-1)
                    }
                    LButton {
                        objectName: "transposeUp"
                        text: "+"
                        implicitHeight: 28
                        radius: 6
                        fontSize: 12
                        Accessible.name: qsTr("Up a semitone")
                        onClicked: root.controller.transposeShown(1)
                    }
                }

                // A video: play it once and stop on its last frame, or loop
                // until it is cleared (kept with the video).
                Row {
                    Layout.alignment: Qt.AlignBottom
                    visible: root.controller.shownIsVideo
                    spacing: 4
                    LButton {
                        objectName: "videoOnce"
                        text: qsTr("Play once")
                        variant: root.controller.shownVideoLoops ? "default" : "primary"
                        implicitHeight: 28
                        radius: 6
                        fontSize: 11
                        onClicked: root.controller.shownVideoLoops = false
                    }
                    LButton {
                        objectName: "videoLoop"
                        text: qsTr("Loop")
                        variant: root.controller.shownVideoLoops ? "primary" : "default"
                        implicitHeight: 28
                        radius: 6
                        fontSize: 11
                        onClicked: root.controller.shownVideoLoops = true
                    }
                }
                // Where it starts and ends (kept with the video): Enter or
                // leaving the box sets them.
                Row {
                    Layout.alignment: Qt.AlignBottom
                    visible: root.controller.shownIsVideo && !root.controller.shownIsAudio
                    spacing: 4
                    component TimeBox: Rectangle {
                        id: box
                        property alias text: field.text
                        property string placeholder
                        signal done()
                        width: 64
                        height: 28
                        radius: 6
                        color: Theme.window
                        border.color: field.activeFocus ? Theme.next : Theme.border
                        TextInput {
                            id: field
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            verticalAlignment: TextInput.AlignVCenter
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            selectByMouse: true
                            onAccepted: { box.done(); focus = false }
                            onActiveFocusChanged: if (!activeFocus) box.done()
                        }
                        LText {
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            visible: field.text === "" && !field.activeFocus
                            text: box.placeholder
                            font.pixelSize: 12
                            color: Theme.textMuted
                        }
                    }
                    LText {
                        height: 28
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("Start")
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                    TimeBox {
                        id: startBox
                        objectName: "videoStart"
                        text: root.controller.shownVideoStart
                        placeholder: "0:00"
                        onDone: if (text !== root.controller.shownVideoStart) root.controller.setShownVideoTrim(startBox.text, endBox.text)
                    }
                    LText {
                        height: 28
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("End")
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                    TimeBox {
                        id: endBox
                        objectName: "videoEnd"
                        text: root.controller.shownVideoEnd
                        placeholder: qsTr("end")
                        onDone: if (text !== root.controller.shownVideoEnd) root.controller.setShownVideoTrim(startBox.text, endBox.text)
                    }
                }

                LButton {
                    objectName: "cuesButton"
                    Layout.alignment: Qt.AlignBottom
                    visible: root.cuesAvailable
                    text: qsTr("Cues")
                    implicitHeight: 28
                    radius: 6
                    fontSize: 11
                    onClicked: root.cuesRequested()
                }

                Row {
                    Layout.alignment: Qt.AlignBottom
                    spacing: 6
                    visible: root.controller.arrangement.length > 0

                    LText {
                        anchors.verticalCenter: parent.verticalCenter
                        rightPadding: 4
                        text: qsTr("Arrangement")
                        font.pixelSize: 12
                        color: Theme.textMuted
                    }
                    Repeater {
                        model: root.controller.arrangement
                        delegate: Rectangle {
                            id: chip
                            required property var modelData
                            width: chipLabel.implicitWidth + 20
                            height: 24
                            radius: 6
                            color: modelData.background
                            LText {
                                id: chipLabel
                                anchors.centerIn: parent
                                text: chip.modelData.label
                                color: chip.modelData.foreground
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
            }

            Grid {
                id: grid
                width: parent.width
                spacing: 14
                columns: Math.max(1, Math.floor((width + spacing) / (Theme.slideMinWidth + spacing)))

                Repeater {
                    model: root.controller.slides
                    delegate: SlideThumb {
                        required property int index
                        required property string group
                        required property color colour
                        required property string text

                        objectName: "slide-" + index
                        width: (grid.width - (grid.columns - 1) * grid.spacing) / grid.columns
                        number: index + 1
                        groupLabel: group
                        groupColor: colour
                        slideText: text
                        status: index === root.controller.liveSlide ? "live"
                              : index === root.controller.previewSlide ? "next" : ""
                        onClicked: {
                            root.controller.activateSlide(index)
                            root.slideClicked(index)
                        }
                    }
                }
            }
        }
    }

    LText {
        anchors.centerIn: parent
        visible: root.controller.slides.count === 0
        text: root.controller.itemTitle === "" ? qsTr("Select an item in the service plan to see its slides.")
                                                : qsTr("This item has no slides.")
        color: Theme.textMuted
    }
}
