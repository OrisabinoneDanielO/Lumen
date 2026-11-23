import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts

// Scripture quick search (Ctrl+B), after design/mockups/Scripture.html.
// Type a reference such as "jn 3 16-18", pick a translation, press Enter to
// show it now (or Add to plan). Esc closes.
Item {
    id: root

    required property QtObject scripture
    signal closed()

    visible: false
    anchors.fill: parent
    z: 50

    function show() {
        visible = true
        reference.selectAll()
        reference.forceActiveFocus()
    }
    function close() {
        visible = false
        root.closed()
    }

    // Dim the window behind; clicking outside closes.
    Rectangle {
        anchors.fill: parent
        color: "#99000000"
        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
        }
    }

    Rectangle {
        id: dialog
        objectName: "scriptureDialog"
        anchors.centerIn: parent
        width: Math.min(960, parent.width - 48)
        height: Math.min(640, parent.height - 48)
        radius: 12
        color: Theme.panel
        border.color: Theme.border
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Scripture")

        MouseArea { anchors.fill: parent } // keep clicks inside

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Reference box.
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 12

                LText {
                    text: qsTr("Scripture")
                    font.pixelSize: 15
                    font.bold: true
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 44
                    radius: 8
                    color: Theme.window
                    border.width: 2
                    border.color: root.scripture.error !== "" && reference.text !== "" ? Theme.live : Theme.next

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 10
                        LText {
                            text: qsTr("Reference")
                            font.pixelSize: 12
                            color: Theme.textMuted
                        }
                        TextInput {
                            id: reference
                            objectName: "scriptureReference"
                            Layout.fillWidth: true
                            color: Theme.text
                            selectionColor: Theme.next
                            font.family: Theme.fontFamily
                            font.pixelSize: 16
                            clip: true
                            Accessible.role: Accessible.EditableText
                            Accessible.name: qsTr("Scripture reference")
                            onTextChanged: root.scripture.lookup(text)
                            onAccepted: {
                                if (root.scripture.showNow())
                                    root.close()
                            }
                            Keys.onEscapePressed: root.close()

                            LText {
                                anchors.fill: parent
                                verticalAlignment: Text.AlignVCenter
                                visible: reference.text === ""
                                text: qsTr("e.g. jn 3 16-18")
                                color: Theme.textMuted
                                font.pixelSize: 16
                            }
                        }
                        LText {
                            objectName: "scriptureLabel"
                            Layout.maximumWidth: 320
                            text: root.scripture.error !== "" && reference.text !== "" ? root.scripture.error
                                                                                       : root.scripture.label
                            color: root.scripture.error !== "" ? Theme.warnText : Theme.textMuted
                            font.pixelSize: 12
                        }
                    }
                }
                LButton {
                    objectName: "scriptureClose"
                    implicitWidth: 36
                    variant: "ghost"
                    text: "×"
                    fontSize: 16
                    Accessible.name: qsTr("Close")
                    onClicked: root.close()
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

            // Translations.
            Flow {
                Layout.fillWidth: true
                Layout.margins: 12
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 6

                LText {
                    height: 32
                    verticalAlignment: Text.AlignVCenter
                    rightPadding: 6
                    text: qsTr("Translation")
                    font.pixelSize: 12
                    color: Theme.textMuted
                }
                Repeater {
                    model: root.scripture.translations
                    delegate: Rectangle {
                        id: chip
                        required property var modelData
                        required property int index
                        readonly property bool chosen: index === root.scripture.translation

                        objectName: "translation-" + modelData.id
                        width: chipLabel.implicitWidth + 24
                        height: 32
                        radius: 6
                        color: chosen ? Theme.rowSelected : Theme.raised
                        border.color: chosen ? Theme.next : Theme.border
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: modelData.title
                        Accessible.checked: chosen

                        LText {
                            id: chipLabel
                            anchors.centerIn: parent
                            text: chip.modelData.abbreviation
                            font.pixelSize: 12
                            font.weight: chip.chosen ? Font.DemiBold : Font.Normal
                            color: chip.chosen ? "#BFD3FF" : Theme.text
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.scripture.selectTranslation(chip.index)
                                reference.forceActiveFocus()
                            }
                        }
                    }
                }
                Rectangle {
                    objectName: "addBible"
                    width: addLabel.implicitWidth + 24
                    height: 32
                    radius: 6
                    color: "transparent"
                    border.color: Theme.border
                    LText {
                        id: addLabel
                        anchors.centerIn: parent
                        text: qsTr("+ Add from folder…")
                        font.pixelSize: 12
                        color: Theme.link
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: folderDialog.active = true
                    }
                }
                LText {
                    height: 32
                    verticalAlignment: Text.AlignVCenter
                    visible: text !== ""
                    text: root.scripture.addSummary
                    font.pixelSize: 12
                    color: Theme.textMuted
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

            // Two equal columns: verses, then preview and options.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                // Verses.
                Flickable {
                    width: parent.width / 2
                    height: parent.height
                    clip: true
                    contentHeight: verseColumn.height + 32
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: verseColumn
                        x: 20
                        y: 16
                        width: parent.width - 40
                        spacing: 8

                        SectionLabel {
                            text: root.scripture.versesPerSlide === 1 ? qsTr("VERSES · ONE PER SLIDE")
                                                                      : qsTr("VERSES · TWO PER SLIDE")
                        }
                        LText {
                            width: parent.width
                            visible: root.scripture.verses.length === 0
                            text: reference.text === "" ? qsTr("Type a reference above, e.g. John 3:16-18 or ps 23.")
                                                        : root.scripture.error
                            wrapMode: Text.WordWrap
                            color: Theme.textMuted
                        }
                        Repeater {
                            model: root.scripture.verses
                            delegate: Rectangle {
                                id: verseRow
                                required property var modelData
                                required property int index
                                objectName: "verse-" + index
                                width: verseColumn.width
                                height: verseText.implicitHeight + 24
                                radius: 8
                                color: index < root.scripture.versesPerSlide ? Theme.rowSelected : Theme.raised
                                border.color: index < root.scripture.versesPerSlide ? Theme.next : "transparent"

                                LText {
                                    id: verseNumber
                                    x: 12
                                    y: 12
                                    text: verseRow.modelData.number
                                    font.bold: true
                                    color: "#BFD3FF"
                                }
                                LText {
                                    id: verseText
                                    x: 12 + Math.max(24, verseNumber.implicitWidth + 12)
                                    y: 12
                                    width: parent.width - x - 12
                                    text: verseRow.modelData.text
                                    wrapMode: Text.WordWrap
                                    elide: Text.ElideNone
                                    lineHeight: 1.4
                                }
                            }
                        }
                    }
                }

                Rectangle { x: parent.width / 2; width: 1; height: parent.height; color: Theme.border }

                // Preview and options.
                ColumnLayout {
                    x: parent.width / 2 + 21
                    y: 16
                    width: parent.width / 2 - 41
                    height: parent.height - 32
                    spacing: 12

                    SectionLabel { text: qsTr("SLIDE PREVIEW") }
                    Rectangle {
                        objectName: "scripturePreview"
                        Layout.fillWidth: true
                        Layout.preferredHeight: width * 9 / 16
                        radius: 8
                        color: "#1B1F2B"
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 24
                            width: parent.width - 48
                            spacing: 10
                            LText {
                                width: parent.width
                                text: root.scripture.previewText
                                color: "#FFFFFF"
                                font.pixelSize: 15
                                wrapMode: Text.WordWrap
                                elide: Text.ElideNone
                                maximumLineCount: 6
                                lineHeight: 1.4
                            }
                            LText {
                                text: root.scripture.previewReference
                                color: "#F2C38B"
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                    LText {
                        objectName: "scriptureCopyright"
                        Layout.fillWidth: true
                        text: root.scripture.copyright
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                    RowLayout {
                        spacing: 6
                        LText {
                            text: qsTr("Verses per slide")
                            font.pixelSize: 12
                            color: Theme.textMuted
                            rightPadding: 6
                        }
                        Repeater {
                            model: [1, 2]
                            delegate: LButton {
                                required property int modelData
                                objectName: "versesPerSlide-" + modelData
                                implicitHeight: 32
                                radius: 6
                                text: String(modelData)
                                variant: root.scripture.versesPerSlide === modelData ? "primary" : "default"
                                onClicked: {
                                    root.scripture.versesPerSlide = modelData
                                    reference.forceActiveFocus()
                                }
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 14
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 8
                Item { Layout.fillWidth: true }
                LButton {
                    objectName: "scriptureAddToPlan"
                    text: qsTr("Add to plan")
                    opacity: root.scripture.label !== "" ? 1 : 0.5
                    onClicked: {
                        if (root.scripture.addToPlan())
                            root.close()
                    }
                }
                LButton {
                    objectName: "scriptureShowNow"
                    text: qsTr("Show now (Enter)")
                    variant: "primary"
                    opacity: root.scripture.label !== "" ? 1 : 0.5
                    onClicked: {
                        if (root.scripture.showNow())
                            root.close()
                    }
                }
            }
        }
    }

    Loader {
        id: folderDialog
        active: false
        sourceComponent: FolderDialog {
            title: qsTr("Add a Bible from a folder of USFM files")
            Component.onCompleted: open()
            onAccepted: {
                root.scripture.addBible(selectedFolder)
                folderDialog.active = false
            }
            onRejected: folderDialog.active = false
        }
    }
}
