import QtQuick
import QtQuick.Layouts

// Bottom drawer: library tabs, search and the song table.
Rectangle {
    id: root

    // Each song: { title, author, copyright, key }
    property var songs: []
    property int selectedIndex: -1

    // Relative column widths: Title, Author, Copyright, Default key.
    readonly property var columnWeights: [2, 1.5, 1, 1]

    component TableRow: Rectangle {
        id: tableRow
        property var cells: []
        property bool header: false
        property bool selected: false

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
                }
            }

            Item {
                Layout.fillWidth: true
            }

            LText {
                text: qsTr("Search")
                font.pixelSize: 12
                color: Theme.textMuted
            }
            SearchField {
                Layout.preferredWidth: 240
                placeholder: qsTr("Title, lyrics or author")
            }
            LButton {
                Layout.leftMargin: 4
                implicitHeight: 32
                radius: 6
                text: qsTr("Import from SongSelect")
            }
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
                    model: root.songs
                    delegate: TableRow {
                        required property var modelData
                        required property int index
                        cells: [modelData.title, modelData.author, modelData.copyright, modelData.key]
                        selected: index === root.selectedIndex
                    }
                }
            }
        }
    }

    LText {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 120
        visible: root.songs.length === 0
        text: qsTr("No songs yet. Import songs to build your library.")
        color: Theme.textMuted
    }
}
