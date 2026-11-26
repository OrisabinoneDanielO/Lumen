import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts

// Bottom drawer: library tabs, search, import and the song table. Clicking a
// song shows its slides; Add to plan appends it to the service plan.
Rectangle {
    id: root

    // Each song: { title, author, copyright, key }
    required property QtObject controller
    readonly property bool searchHasFocus: search.inputFocused
    signal searchDone()
    signal themesRequested()

    // Relative column widths: Title, Author, Copyright, Default key.
    readonly property var columnWeights: [2, 1.5, 1, 1]

    component TableRow: Rectangle {
        id: tableRow
        property var cells: []
        property bool header: false
        property bool selected: false
        signal clicked()

        width: parent ? parent.width : 0
        height: header ? 34 : 30
        radius: 6
        color: selected ? Theme.rowSelected : "transparent"

        Row {
            anchors.fill: parent
            Repeater {
                model: tableRow.cells
                delegate: LText {
                    required property string modelData
                    required property int index
                    width: tableRow.width * root.columnWeights[index] / 5.5
                    height: tableRow.height
                    leftPadding: 6
                    rightPadding: 6
                    verticalAlignment: Text.AlignVCenter
                    text: modelData
                    font.pixelSize: 12
                    font.weight: tableRow.header ? Font.DemiBold : Font.Normal
                    color: tableRow.header ? Theme.textMuted : Theme.text
                }
            }
        }
        MouseArea {
            anchors.fill: parent
            enabled: !tableRow.header
            cursorShape: Qt.PointingHandCursor
            onClicked: tableRow.clicked()
        }
    }

    color: Theme.panel

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 1
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.bottomMargin: 14
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Repeater {
                model: [qsTr("Songs"), qsTr("Scripture"), qsTr("Media"), qsTr("Presentations"),
                        qsTr("Themes")]

                delegate: Item {
                    id: tab
                    required property string modelData
                    required property int index
                    readonly property bool active: index === 0
                    // Songs are listed here; Scripture and Themes open their
                    // panels; Media and Presentations arrive in batches 9-10.
                    readonly property bool available: index !== 2 && index !== 3
                    objectName: "libraryTab-" + index
                    opacity: available ? 1 : 0.45

                    implicitWidth: tabLabel.implicitWidth + 28
                    implicitHeight: 40
                    Accessible.role: Accessible.PageTab
                    Accessible.name: modelData

                    LText {
                        id: tabLabel
                        anchors.centerIn: parent
                        text: tab.modelData
                        font.weight: tab.active ? Font.DemiBold : Font.Normal
                        color: tab.active ? Theme.text : Theme.textMuted
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 2
                        visible: tab.active
                        color: Theme.live
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: tab.available && !tab.active
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (tab.index === 1)
                                root.controller.scriptureRequested()
                            else if (tab.index === 4)
                                root.themesRequested()
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
            }

            // Last import's result; click for the details.
            LText {
                objectName: "importSummary"
                Layout.maximumWidth: 260
                visible: text !== ""
                text: root.controller.importSummary
                font.pixelSize: 12
                color: Theme.textMuted
                MouseArea {
                    anchors.fill: parent
                    enabled: root.controller.importDetails !== ""
                    cursorShape: Qt.PointingHandCursor
                    onClicked: details.visible = !details.visible
                }
            }
            LButton {
                objectName: "addToPlan"
                visible: root.controller.libraryAvailable
                implicitHeight: 32
                radius: 6
                text: qsTr("Add to plan")
                opacity: root.controller.selectedSong >= 0 ? 1.0 : 0.5
                onClicked: root.controller.addSongToPlan(root.controller.selectedSong)
            }
            LButton {
                objectName: "importSongs"
                visible: root.controller.libraryAvailable
                implicitHeight: 32
                radius: 6
                text: qsTr("Import songs…")
                onClicked: fileDialog.active = true
            }
            LButton {
                objectName: "importFolder"
                visible: root.controller.libraryAvailable
                implicitHeight: 32
                radius: 6
                text: qsTr("Import folder…")
                onClicked: folderDialog.active = true
            }
            LText {
                Layout.leftMargin: 8
                text: qsTr("Search")
                font.pixelSize: 12
                color: Theme.textMuted
            }
            SearchField {
                id: search
                objectName: "librarySearch"
                onEscaped: root.searchDone()
                onTextChanged: root.controller.searchSongs(text)
                Layout.preferredWidth: 240
                placeholder: qsTr("Title, lyrics or author")
            }
            // Import from SongSelect arrives with batch 17 (CCLI agreement).
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: table.height
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: table
                width: parent.width

                TableRow {
                    header: true
                    cells: [qsTr("Title"), qsTr("Author"), qsTr("Copyright"), qsTr("Default key")]
                }
                Repeater {
                    model: root.controller.songs
                    delegate: TableRow {
                        required property int index
                        required property string title
                        required property string author
                        required property string copyright
                        required property string key
                        objectName: "songRow-" + index
                        cells: [title, author, copyright, key]
                        selected: index === root.controller.selectedSong
                        onClicked: root.controller.showSong(index)
                    }
                }
            }
        }
    }

    LText {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 120
        visible: root.controller.songs.count === 0
        text: search.text !== "" ? qsTr("No songs match \"%1\".").arg(search.text)
                                 : qsTr("No songs yet. Import songs to build your library.")
        color: Theme.textMuted
    }

    // Import details, shown when the summary is clicked.
    Rectangle {
        id: details
        objectName: "importDetails"
        visible: false
        anchors.right: parent.right
        anchors.rightMargin: 16
        y: 44
        width: Math.min(560, parent.width - 32)
        height: Math.min(detailsText.implicitHeight + 24, parent.height - 56)
        radius: 8
        color: Theme.raised
        border.color: Theme.border
        z: 10
        LText {
            id: detailsText
            anchors.fill: parent
            anchors.margins: 12
            text: root.controller.importDetails
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            font.pixelSize: 12
        }
        MouseArea {
            anchors.fill: parent
            onClicked: details.visible = false
        }
    }

    // Dialogs are created only when used.
    Loader {
        id: fileDialog
        active: false
        sourceComponent: FileDialog {
            title: qsTr("Import songs")
            fileMode: FileDialog.OpenFiles
            nameFilters: [qsTr("Song files (*.txt *.xml)"), qsTr("All files (*)")]
            Component.onCompleted: open()
            onAccepted: {
                root.controller.importSongs(selectedFiles)
                fileDialog.active = false
            }
            onRejected: fileDialog.active = false
        }
    }
    Loader {
        id: folderDialog
        active: false
        sourceComponent: FolderDialog {
            title: qsTr("Import every song in a folder")
            Component.onCompleted: open()
            onAccepted: {
                root.controller.importSongs([selectedFolder])
                folderDialog.active = false
            }
            onRejected: folderDialog.active = false
        }
    }
}
