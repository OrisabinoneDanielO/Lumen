import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window

// Scripture quick search (Ctrl+B), after design/mockups/Scripture.html, in a
// window of its own: it stays open beside the operator window, so passages
// can be looked up one after another while the rest of Lumen is used.
// Type a reference such as "jn 3 16-18" and pick a translation; Enter puts it
// in Preview, Shift+Enter (or a double-click on the passage) shows it now.
// Esc closes it.
Window {
    id: root

    required property QtObject scripture
    signal closed()

    title: qsTr("Scripture · Lumen Presenter")
    width: 900
    height: 600
    minimumWidth: 640
    minimumHeight: 420
    visible: false
    color: Theme.panel

    // The reference as the three parts make it: "John 3:16-18". (A function,
    // read fresh: a binding may not have caught up within a key press.)
    function composed() {
        const book = bookInput.text.trim()
        if (book === "")
            return ""
        const chapter = chapterInput.text.trim()
        if (chapter === "")
            return book
        const verse = verseInput.text.trim()
        return verse === "" ? book + " " + chapter : book + " " + chapter + ":" + verse
    }
    readonly property string referenceText: root.scripture.query
    // Only the book so far: say how to go on rather than show an error.
    readonly property bool suggesting: chapterInput.text === "" && root.scripture.bookFor(bookInput.text) !== ""
    readonly property string hint: qsTr("%1: Space for the chapter").arg(root.scripture.bookFor(bookInput.text))

    function refresh() { root.scripture.lookup(root.composed()) }
    // Into a part, all of it highlighted to type over.
    function focusPart(part) {
        part.forceActiveFocus()
        part.selectAll()
    }
    // The book typed becomes its full name; on to the chapter.
    function acceptBook() {
        const full = root.scripture.bookFor(bookInput.text)
        if (full !== "")
            bookInput.text = full
        focusPart(chapterInput)
        refresh()
    }
    // Keys every part shares: Up to the book, Enter for Preview, Shift+Enter
    // to show it now, Escape to close.
    function handleCommonKey(event) {
        if (event.key === Qt.Key_Up) {
            focusPart(bookInput)
            return true
        }
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (bookInput.activeFocus && root.scripture.bookFor(bookInput.text) !== "")
                bookInput.text = root.scripture.bookFor(bookInput.text)
            // Only a book: its first verse ("j" and Enter is John 1:1).
            if (chapterInput.text === "") {
                chapterInput.text = "1"
                if (verseInput.text === "")
                    verseInput.text = "1"
            }
            refresh()
            if (event.modifiers & Qt.ShiftModifier)
                root.scripture.showNow()
            else
                root.scripture.preview()
            return true
        }
        if (event.key === Qt.Key_Escape) {
            root.close()
            return true
        }
        return false
    }

    function show() {
        visible = true
        raise()
        requestActivate()
        // The last passage stays; the book is highlighted for a new one.
        focusPart(bookInput)
    }
    function close() {
        visible = false
        root.closed()
    }
    onClosing: root.closed()

    Rectangle {
        id: dialog
        objectName: "scriptureDialog"
        anchors.fill: parent
        color: Theme.panel
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Scripture")

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
                    border.color: root.scripture.error !== "" && root.referenceText !== "" && !root.suggesting ? Theme.live : Theme.next

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
                        // The reference in three parts that stay put: book,
                        // chapter, verse(s). Typing the book fills in its name
                        // ("j" -> John); Space, Tab or Right moves on to the
                        // chapter and then the verse, each highlighted to type
                        // over; Left goes back one part, Up to the book.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            TextInput {
                                id: bookInput
                                objectName: "scriptureReference"
                                Layout.preferredWidth: Math.max(contentWidth + 2, 40)
                                Layout.leftMargin: 5
                                color: Theme.text
                                selectionColor: Theme.next
                                selectedTextColor: Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: 16
                                Accessible.role: Accessible.EditableText
                                Accessible.name: qsTr("Book")
                                // Each part looks like one: a quiet field, brighter
                                // while it has the keyboard.
                                Rectangle {
                                    z: -1
                                    anchors.fill: parent
                                    anchors.leftMargin: -5
                                    anchors.rightMargin: -5
                                    anchors.topMargin: -4
                                    anchors.bottomMargin: -4
                                    radius: 5
                                    color: parent.activeFocus ? Theme.selected : Theme.raised
                                    border.color: parent.activeFocus ? Theme.next : "transparent"
                                }
                                property bool skipFill: false
                                // Fills in the rest of the book's name, highlighted,
                                // so typing on simply replaces it.
                                onTextEdited: {
                                    if (skipFill) {
                                        skipFill = false
                                    } else {
                                        const full = root.scripture.bookFor(text)
                                        const n = text.length
                                        if (full !== "" && n < full.length && full.toLowerCase().startsWith(text.toLowerCase())) {
                                            text = full
                                            select(n, full.length)
                                        }
                                    }
                                    root.refresh()
                                }
                                Keys.onPressed: event => {
                                    if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                                        skipFill = true
                                    } else if (event.key === Qt.Key_Space || event.key === Qt.Key_Tab
                                               || (event.key === Qt.Key_Right && (selectedText !== "" || cursorPosition === text.length))) {
                                        root.acceptBook()
                                        event.accepted = true
                                    } else if (/^[0-9]$/.test(event.text) && /[A-Za-z\u00C0-\u024F\u1E00-\u1EFF]/.test(text)) {
                                        // "jn3": the number is the chapter.
                                        root.acceptBook()
                                        chapterInput.text = event.text
                                        root.refresh()
                                        event.accepted = true
                                    } else if (root.handleCommonKey(event)) {
                                        event.accepted = true
                                    }
                                }

                                LText {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: bookInput.text === ""
                                    text: qsTr("Book")
                                    color: Theme.textMuted
                                    font.pixelSize: 16
                                }
                            }
                            // An abbreviation ("jn"): the book it means.
                            LText {
                                objectName: "scriptureSuggestion"
                                readonly property string full: bookInput.activeFocus ? root.scripture.bookFor(bookInput.text) : ""
                                visible: full !== "" && !full.toLowerCase().startsWith(bookInput.text.toLowerCase())
                                text: "→ " + full + "  "
                                color: Theme.textMuted
                                font.pixelSize: 16
                            }
                            TextInput {
                                id: chapterInput
                                objectName: "scriptureChapter"
                                Layout.preferredWidth: Math.max(contentWidth + 2, 12)
                                Layout.leftMargin: 14
                                color: Theme.text
                                selectionColor: Theme.next
                                selectedTextColor: Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: 16
                                validator: RegularExpressionValidator { regularExpression: /[0-9]{0,3}/ }
                                Accessible.role: Accessible.EditableText
                                Accessible.name: qsTr("Chapter")
                                // Each part looks like one: a quiet field, brighter
                                // while it has the keyboard.
                                Rectangle {
                                    z: -1
                                    anchors.fill: parent
                                    anchors.leftMargin: -5
                                    anchors.rightMargin: -5
                                    anchors.topMargin: -4
                                    anchors.bottomMargin: -4
                                    radius: 5
                                    color: parent.activeFocus ? Theme.selected : Theme.raised
                                    border.color: parent.activeFocus ? Theme.next : "transparent"
                                }
                                onTextEdited: root.refresh()
                                Keys.onPressed: event => {
                                    if (event.key === Qt.Key_Space || event.key === Qt.Key_Tab || event.key === Qt.Key_Right
                                        || event.text === ":" || event.text === ".") {
                                        root.focusPart(verseInput)
                                        event.accepted = true
                                    } else if (event.key === Qt.Key_Left || (event.key === Qt.Key_Backspace && text === "")) {
                                        root.focusPart(bookInput)
                                        event.accepted = true
                                    } else if (root.handleCommonKey(event)) {
                                        event.accepted = true
                                    }
                                }
                                LText {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: chapterInput.text === ""
                                    text: "1"
                                    color: Theme.border
                                    font.pixelSize: 16
                                }
                            }
                            LText {
                                Layout.leftMargin: 7
                                Layout.rightMargin: 7
                                text: ":"
                                color: Theme.textMuted
                                font.pixelSize: 16
                            }
                            TextInput {
                                id: verseInput
                                objectName: "scriptureVerse"
                                Layout.preferredWidth: Math.max(contentWidth + 2, 28)
                                color: Theme.text
                                selectionColor: Theme.next
                                selectedTextColor: Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: 16
                                validator: RegularExpressionValidator { regularExpression: /[0-9:\-–]{0,12}/ }
                                Accessible.role: Accessible.EditableText
                                Accessible.name: qsTr("Verses")
                                // Each part looks like one: a quiet field, brighter
                                // while it has the keyboard.
                                Rectangle {
                                    z: -1
                                    anchors.fill: parent
                                    anchors.leftMargin: -5
                                    anchors.rightMargin: -5
                                    anchors.topMargin: -4
                                    anchors.bottomMargin: -4
                                    radius: 5
                                    color: parent.activeFocus ? Theme.selected : Theme.raised
                                    border.color: parent.activeFocus ? Theme.next : "transparent"
                                }
                                onTextEdited: root.refresh()
                                Keys.onPressed: event => {
                                    if (event.key === Qt.Key_Left || (event.key === Qt.Key_Backspace && text === "")) {
                                        root.focusPart(chapterInput)
                                        event.accepted = true
                                    } else if (root.handleCommonKey(event)) {
                                        event.accepted = true
                                    }
                                }
                                LText {
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: verseInput.text === ""
                                    text: qsTr("all")
                                    color: Theme.border
                                    font.pixelSize: 16
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }
                        LText {
                            objectName: "scriptureLabel"
                            Layout.maximumWidth: 320
                            text: root.suggesting ? root.hint
                                  : root.scripture.error !== "" && root.referenceText !== "" ? root.scripture.error
                                                                                       : root.scripture.label
                            color: root.scripture.error !== "" && !root.suggesting ? Theme.warnText : Theme.textMuted
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
                                verseInput.forceActiveFocus()
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
                            text: root.referenceText === "" ? qsTr("Type a book above (j for John), then the chapter and verse.")
                                  : root.suggesting ? root.hint
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
                        // Click: in Preview. Double-click: on the screen now.
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.scripture.preview()
                            onDoubleClicked: root.scripture.showNow()
                        }
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
                                    verseInput.forceActiveFocus()
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
                    enabled: root.scripture.label !== ""
                    onClicked: root.scripture.addToPlan()
                }
                LButton {
                    objectName: "scripturePreviewButton"
                    text: qsTr("Preview (Enter)")
                    enabled: root.scripture.label !== ""
                    onClicked: root.scripture.preview()
                }
                LButton {
                    objectName: "scriptureShowNow"
                    text: qsTr("Show now (Shift+Enter)")
                    variant: "primary"
                    enabled: root.scripture.label !== ""
                    onClicked: root.scripture.showNow()
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
