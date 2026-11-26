import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts

// Bottom drawer: library tabs, search, import and the song table. Clicking a
// song shows its slides; Add to plan appends it to the service plan. The
// Media tab lists imported videos and pictures as cards, and the
// Presentations tab imported slide decks (PowerPoint, PDF).
Rectangle {
    id: root

    // Each song: { title, author, copyright, key }
    required property QtObject controller
    readonly property bool searchHasFocus: search.inputFocused
    signal searchDone()
    signal picked() // a row or card clicked: the live keys work again
    onPicked: search.leave()
    signal themesRequested()
    // What follows the pointer while dragging onto the plan (Main.qml).
    property Item dragGhost: null
    // Scripture and Themes are separate parts (null when not loaded).
    property bool scriptureAvailable: true
    property bool themesAvailable: true
    // + Add in the plan: a tab, with the search ready for songs.
    function showPage(p) {
        page = p
        if (p === 0)
            search.focusInput()
    }

    // Press, then move: a drag onto the service plan instead of a click.
    component DragMouse: MouseArea {
        id: dm
        property string dragSource
        property int dragRow: -1
        property string dragTitle
        property point pressAt
        property bool dragged: false
        signal tapped()
        signal doubleTapped()
        preventStealing: true
        onPressed: mouse => {
            pressAt = Qt.point(mouse.x, mouse.y)
            dragged = false
        }
        onPositionChanged: mouse => {
            const ghost = root.dragGhost
            if (!pressed || !ghost || dragSource === "")
                return
            if (dragged) {
                ghost.place(dm, mouse.x, mouse.y)
            } else if (Math.abs(mouse.x - pressAt.x) + Math.abs(mouse.y - pressAt.y) >= ghost.threshold) {
                dragged = true
                ghost.begin(dm, mouse.x, mouse.y, dragSource, dragRow, dragTitle)
            }
        }
        onReleased: if (dragged) root.dragGhost.finish()
        onCanceled: if (dragged) root.dragGhost.finish()
        onClicked: if (!dragged) tapped()
        onDoubleClicked: if (!dragged) doubleTapped()
    }
    // 0: Songs, 2: Media, 3: Presentations (Scripture and Themes open their
    // own panels).
    property int page: 0

    // Relative column widths: Title, Author, Copyright, Default key.
    readonly property var columnWeights: [2, 1.5, 1, 1]

    // A card with a 16:9 picture, a corner badge and a title.
    component Card: Rectangle {
        id: card
        property string title
        property url thumbnail
        property string badge
        property bool selected: false
        property string dragSource // "media" or "deck"
        property int dragRow: -1
        signal hovered()
        signal doubleClicked()
        signal clicked()
        width: 168
        height: 124
        radius: 8
        color: Theme.raised
        border.width: 2
        border.color: selected ? Theme.next : Theme.border
        Accessible.role: Accessible.Button
        Accessible.name: title

        Rectangle {
            x: 2
            y: 2
            width: parent.width - 4
            height: Math.round(width * 9 / 16)
            radius: 6
            color: "#000000"
            Image {
                anchors.fill: parent
                source: card.thumbnail
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
            LText {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 4
                visible: card.badge !== ""
                text: card.badge
                font.pixelSize: 10
                color: "#FFFFFF"
                style: Text.Outline
                styleColor: "#000000"
            }
        }
        LText {
            x: 8
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8
            width: parent.width - 16
            text: card.title
            font.pixelSize: 12
        }
        DragMouse {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            dragSource: card.dragSource
            dragRow: card.dragRow
            dragTitle: card.title
            onTapped: card.clicked()
            onDoubleTapped: card.doubleClicked()
            onContainsMouseChanged: if (containsMouse) card.hovered()
        }
    }

    component TableRow: Rectangle {
        id: tableRow
        property var cells: []
        property bool header: false
        property bool selected: false
        property int dragRow: -1 // a song row: dragged onto the plan
        signal doubleClicked()
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
        DragMouse {
            anchors.fill: parent
            enabled: !tableRow.header
            cursorShape: Qt.PointingHandCursor
            dragSource: tableRow.dragRow >= 0 ? "song" : ""
            dragRow: tableRow.dragRow
            dragTitle: tableRow.cells.length > 0 ? tableRow.cells[0] : ""
            onTapped: tableRow.clicked()
            onDoubleTapped: tableRow.doubleClicked()
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
                    readonly property bool active: index === root.page
                    // Songs, Media and Presentations are listed here;
                    // Scripture and Themes open their panels.
                    readonly property bool available: (index !== 1 || root.scriptureAvailable)
                                                      && (index !== 2 || root.controller.mediaAvailable)
                                                      && (index !== 3 || root.controller.decksAvailable)
                                                      && (index !== 4 || root.themesAvailable)
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
                            if (tab.index === 0 || tab.index === 2 || tab.index === 3)
                                root.page = tab.index
                            else if (tab.index === 1)
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
                visible: text !== "" && root.page === 0
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
            LText {
                objectName: "mediaSummary"
                Layout.maximumWidth: 260
                visible: text !== "" && root.page === 2
                text: root.controller.mediaSummary
                font.pixelSize: 12
                color: Theme.textMuted
            }
            LButton {
                objectName: "mediaToPlan"
                visible: root.page === 2
                implicitHeight: 32
                radius: 6
                text: qsTr("Add to plan")
                enabled: root.controller.selectedMedia >= 0
                onClicked: root.controller.addMediaToPlan(root.controller.selectedMedia)
            }
            LButton {
                objectName: "useAsBackground"
                visible: root.page === 2
                implicitHeight: 32
                radius: 6
                text: qsTr("Use as background")
                enabled: root.controller.selectedMedia >= 0
                onClicked: root.controller.useAsBackground(root.controller.selectedMedia)
            }
            LButton {
                objectName: "importMedia"
                visible: root.page === 2
                implicitHeight: 32
                radius: 6
                text: qsTr("Import media…")
                onClicked: mediaDialog.active = true
            }
            LText {
                objectName: "deckSummary"
                Layout.maximumWidth: 260
                visible: text !== "" && root.page === 3
                text: root.controller.deckSummary
                font.pixelSize: 12
                color: Theme.textMuted
                MouseArea {
                    anchors.fill: parent
                    enabled: root.controller.deckDetails !== ""
                    cursorShape: Qt.PointingHandCursor
                    onClicked: deckDetails.visible = !deckDetails.visible
                }
            }
            LButton {
                objectName: "deckToPlan"
                visible: root.page === 3
                implicitHeight: 32
                radius: 6
                text: qsTr("Add to plan")
                enabled: root.controller.selectedDeck >= 0
                onClicked: root.controller.addDeckToPlan(root.controller.selectedDeck)
            }
            LButton {
                objectName: "importDecks"
                visible: root.page === 3
                implicitHeight: 32
                radius: 6
                text: root.controller.decksBusy ? qsTr("Importing…") : qsTr("Import presentation…")
                opacity: root.controller.decksBusy ? 0.5 : 1.0
                onClicked: if (!root.controller.decksBusy) deckDialog.active = true
            }
            LButton {
                objectName: "addToPlan"
                visible: root.controller.libraryAvailable && root.page === 0
                implicitHeight: 32
                radius: 6
                text: qsTr("Add to plan")
                enabled: root.controller.selectedSong >= 0
                onClicked: root.controller.addSongToPlan(root.controller.selectedSong)
            }
            LButton {
                objectName: "importSongs"
                visible: root.controller.libraryAvailable && root.page === 0
                implicitHeight: 32
                radius: 6
                text: qsTr("Import songs…")
                onClicked: fileDialog.active = true
            }
            LButton {
                objectName: "importFolder"
                visible: root.controller.libraryAvailable && root.page === 0
                implicitHeight: 32
                radius: 6
                text: qsTr("Import folder…")
                onClicked: folderDialog.active = true
            }
            LText {
                Layout.leftMargin: 8
                visible: root.page === 0
                text: qsTr("Search")
                font.pixelSize: 12
                color: Theme.textMuted
            }
            SearchField {
                id: search
                objectName: "librarySearch"
                visible: root.page === 0
                onEscaped: root.searchDone()
                // Return: the first song found, in Preview; the keyboard goes back to the show.
                onAccepted: {
                    if (root.controller.songs.count > 0) {
                        root.controller.showSong(0)
                        root.picked()
                    }
                }
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
            visible: root.page === 0
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
                        dragRow: index
                        selected: index === root.controller.selectedSong
                        onClicked: { root.picked(); root.controller.showSong(index) }
                        onDoubleClicked: root.controller.playSong(index)
                    }
                }
            }
        }

        // Media: one card per video or picture. Click to show it in the
        // slide grid (then send it live like a slide).
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 10
            visible: root.page === 2
            clip: true
            contentHeight: mediaGrid.height
            boundsBehavior: Flickable.StopAtBounds

            Flow {
                id: mediaGrid
                width: parent.width
                spacing: 10

                Repeater {
                    model: root.controller.media
                    delegate: Card {
                        required property int index
                        required title
                        required thumbnail
                        required property string duration
                        required property bool loop
                        objectName: "mediaCard-" + index
                        dragSource: "media"
                        dragRow: index
                        badge: duration === "" ? "" : loop ? qsTr("Loop · %1").arg(duration) : duration
                        selected: index === root.controller.selectedMedia
                        // Click: in Preview. Double-click: on the screen now.
                        // Pointed at: the output opens it already, so it
                        // starts at once when it goes live.
                        onClicked: { root.picked(); root.controller.showMedia(index) }
                        onDoubleClicked: root.controller.playMedia(index)
                        onHovered: root.controller.preloadMedia(index)
                    }
                }
            }
        }

        // Presentations: one card per deck, showing its first slide.
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 10
            visible: root.page === 3
            clip: true
            contentHeight: deckGrid.height
            boundsBehavior: Flickable.StopAtBounds

            Flow {
                id: deckGrid
                width: parent.width
                spacing: 10

                Repeater {
                    model: root.controller.decks
                    delegate: Card {
                        required property int index
                        required title
                        required thumbnail
                        required property int slides
                        objectName: "deckCard-" + index
                        dragSource: "deck"
                        dragRow: index
                        badge: slides === 1 ? qsTr("1 slide") : qsTr("%1 slides").arg(slides)
                        selected: index === root.controller.selectedDeck
                        onClicked: { root.picked(); root.controller.showDeck(index) }
                    }
                }
            }
        }
    }

    LText {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 120
        visible: root.page === 3 && root.controller.decks.count === 0
        text: qsTr("No presentations yet. Import PowerPoint or PDF files to show them as slides.")
        color: Theme.textMuted
    }

    LText {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 120
        visible: root.page === 2 && root.controller.media.count === 0
        text: qsTr("No media yet. Import videos and pictures to use them in services.")
        color: Theme.textMuted
    }

    LText {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 120
        visible: root.page === 0 && root.controller.songs.count === 0
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

    // Presentation import details, shown when the summary is clicked.
    Rectangle {
        id: deckDetails
        objectName: "deckDetails"
        visible: false
        anchors.right: parent.right
        anchors.rightMargin: 16
        y: 44
        width: Math.min(560, parent.width - 32)
        height: Math.min(deckDetailsText.implicitHeight + 24, parent.height - 56)
        radius: 8
        color: Theme.raised
        border.color: Theme.border
        z: 10
        LText {
            id: deckDetailsText
            anchors.fill: parent
            anchors.margins: 12
            text: root.controller.deckDetails
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            font.pixelSize: 12
        }
        MouseArea {
            anchors.fill: parent
            onClicked: deckDetails.visible = false
        }
    }

    // Dialogs are created only when used.
    Loader {
        id: fileDialog
        active: false
        sourceComponent: FileDialog {
            title: qsTr("Import songs")
            fileMode: FileDialog.OpenFiles
            nameFilters: [qsTr("Songs and libraries (*.txt *.xml *.pro6 *.pro5 songs.sqlite Songs.db)"),
                          qsTr("OpenLP library (songs.sqlite)"), qsTr("EasyWorship library (Songs.db)"),
                          qsTr("ProPresenter 5 and 6 (*.pro6 *.pro5)"), qsTr("All files (*)")]
            Component.onCompleted: open()
            onAccepted: {
                root.controller.importSongs(selectedFiles)
                fileDialog.active = false
            }
            onRejected: fileDialog.active = false
        }
    }
    Loader {
        id: mediaDialog
        active: false
        sourceComponent: FileDialog {
            title: qsTr("Import videos and pictures")
            fileMode: FileDialog.OpenFiles
            nameFilters: [qsTr("Videos and pictures (*.mp4 *.m4v *.mov *.webm *.mkv *.avi *.mpg *.mpeg *.wmv *.png *.jpg *.jpeg *.bmp *.gif *.webp)"),
                          qsTr("All files (*)")]
            Component.onCompleted: open()
            onAccepted: {
                root.controller.importMedia(selectedFiles)
                mediaDialog.active = false
            }
            onRejected: mediaDialog.active = false
        }
    }
    Loader {
        id: deckDialog
        active: false
        sourceComponent: FileDialog {
            title: qsTr("Import presentations")
            fileMode: FileDialog.OpenFiles
            nameFilters: [qsTr("Presentations (*.pptx *.ppsx *.ppt *.pps *.odp *.pdf)"), qsTr("All files (*)")]
            Component.onCompleted: open()
            onAccepted: {
                root.controller.importPresentations(selectedFiles)
                deckDialog.active = false
            }
            onRejected: deckDialog.active = false
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
