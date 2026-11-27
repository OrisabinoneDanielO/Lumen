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
    function escaped(text) {
        return String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }

    // Many Bibles: up to eight show as buttons (the chosen one, those used
    // lately, then the bundled order), the rest in the list (All
    // translations), grouped by language.
    property var recentIds: []
    property string listMode: "" // "", "primary" or "second": the list is open
    property string listFilter: ""
    function quickTranslations(exclude) {
        const all = root.scripture.translations.map((t, i) => Object.assign({ index: i }, t))
        const picked = []
        const add = t => {
            if (t && t.index !== exclude && !picked.some(p => p.id === t.id) && picked.length < 8)
                picked.push(t)
        }
        if (root.scripture.translation !== exclude)
            add(all[root.scripture.translation])
        if (root.scripture.secondTranslation >= 0 && exclude >= 0)
            add(all[root.scripture.secondTranslation])
        for (const id of root.recentIds)
            add(all.find(t => t.id === id))
        for (const t of all)
            add(t)
        return picked.sort((a, b) => a.index - b.index) // buttons stay in place
    }
    function remember(index) {
        const t = root.scripture.translations[index]
        if (t)
            root.recentIds = [t.id].concat(root.recentIds.filter(id => id !== t.id)).slice(0, 8)
    }
    // The list: language headings, then that language's translations.
    function listRows() {
        const f = root.listFilter.trim().toLowerCase()
        const rows = root.scripture.translations.map((t, i) => Object.assign({ index: i, heading: false }, t))
            .filter(t => f === "" || (t.abbreviation + " " + t.title + " " + t.language).toLowerCase().includes(f))
            .filter(t => root.listMode !== "second" || t.index !== root.scripture.translation)
        rows.sort((a, b) => a.language === b.language ? a.index - b.index : a.language.localeCompare(b.language))
        const out = []
        for (const r of rows) {
            if (out.length === 0 || out[out.length - 1].language !== r.language)
                out.push({ heading: true, language: r.language, id: "h-" + r.language, index: -1, abbreviation: "", title: "" })
            out.push(r)
        }
        return out
    }
    function pickFromList(index) {
        if (root.listMode === "second")
            root.scripture.secondTranslation = index
        else
            root.scripture.selectTranslation(index)
        root.remember(index)
        root.listMode = ""
        root.listFilter = ""
        verseInput.forceActiveFocus()
    }
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
                    model: root.quickTranslations(-1)
                    delegate: Rectangle {
                        id: chip
                        required property var modelData
                        readonly property int index: modelData.index
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
                                root.remember(chip.index)
                                verseInput.forceActiveFocus()
                            }
                        }
                    }
                }
                Rectangle {
                    objectName: "translationMore"
                    visible: root.scripture.translations.length > 8
                    width: moreLabel.implicitWidth + 24
                    height: 32
                    radius: 6
                    color: Theme.raised
                    border.color: Theme.border
                    LText {
                        id: moreLabel
                        anchors.centerIn: parent
                        text: qsTr("All translations (%1) ▾").arg(root.scripture.translations.length)
                        font.pixelSize: 12
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.listMode = root.listMode === "primary" ? "" : "primary"
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
                // One file: Zefania or OpenSong XML, USFX, or OpenLP's
                // verses.csv (with books.csv beside it).
                Rectangle {
                    objectName: "addBibleFile"
                    width: addFileLabel.implicitWidth + 24
                    height: 32
                    radius: 6
                    color: "transparent"
                    border.color: Theme.border
                    LText {
                        id: addFileLabel
                        anchors.centerIn: parent
                        text: qsTr("+ Add from file…")
                        font.pixelSize: 12
                        color: Theme.link
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: fileDialog.active = true
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

            // A second translation side by side (e.g. English and Yorùbá).
            Flow {
                objectName: "sideBySideRow"
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.bottomMargin: 12
                spacing: 6
                visible: root.scripture.translations.length > 1

                LText {
                    height: 32
                    verticalAlignment: Text.AlignVCenter
                    rightPadding: 6
                    text: qsTr("Side by side with")
                    font.pixelSize: 12
                    color: Theme.textMuted
                }
                Repeater {
                    // "Off", then every translation but the chosen one.
                    model: [{ index: -1, abbreviation: qsTr("Off"), id: "off" }].concat(
                               root.quickTranslations(root.scripture.translation))
                    delegate: Rectangle {
                        id: secondChip
                        required property var modelData
                        readonly property bool chosen: modelData.index === root.scripture.secondTranslation
                                                       || (modelData.index === -1 && root.scripture.secondTranslation === root.scripture.translation)
                        objectName: "second-" + modelData.id
                        width: secondLabel.implicitWidth + 24
                        height: 32
                        radius: 6
                        color: chosen ? Theme.rowSelected : Theme.raised
                        border.color: chosen ? Theme.next : Theme.border
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: qsTr("Side by side with %1").arg(modelData.abbreviation)
                        Accessible.checked: chosen
                        LText {
                            id: secondLabel
                            anchors.centerIn: parent
                            text: secondChip.modelData.abbreviation
                            font.pixelSize: 12
                            font.weight: secondChip.chosen ? Font.DemiBold : Font.Normal
                            color: secondChip.chosen ? "#BFD3FF" : Theme.text
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.scripture.secondTranslation = secondChip.modelData.index
                                if (secondChip.modelData.index >= 0)
                                    root.remember(secondChip.modelData.index)
                                verseInput.forceActiveFocus()
                            }
                        }
                    }
                }
                Rectangle {
                    objectName: "secondMore"
                    visible: root.scripture.translations.length > 8
                    width: secondMoreLabel.implicitWidth + 24
                    height: 32
                    radius: 6
                    color: Theme.raised
                    border.color: Theme.border
                    LText {
                        id: secondMoreLabel
                        anchors.centerIn: parent
                        text: qsTr("Other… ▾")
                        font.pixelSize: 12
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.listMode = root.listMode === "second" ? "" : "second"
                    }
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

                        // Finding a verse by its words ("love one another";
                        // a phrase in quotes), in the chosen translation.
                        SearchField {
                            id: findField
                            objectName: "scriptureFind"
                            inputName: "scriptureFindText"
                            width: parent.width
                            placeholder: qsTr("Find words, e.g. love one another")
                            onTextChanged: findTimer.restart()
                            onAccepted: { findTimer.stop(); root.scripture.findWords(text) }
                            Timer {
                                id: findTimer
                                interval: 250
                                onTriggered: root.scripture.findWords(findField.text)
                            }
                        }
                        LText {
                            objectName: "scriptureFindSummary"
                            width: parent.width
                            visible: root.scripture.wordQuery.trim() !== ""
                            text: root.scripture.wordSummary
                            font.pixelSize: 11
                            color: Theme.textMuted
                        }
                        Repeater {
                            model: root.scripture.wordQuery.trim() !== "" ? root.scripture.wordResults : []
                            delegate: Rectangle {
                                id: hitRow
                                required property var modelData
                                required property int index
                                objectName: "wordResult-" + index
                                readonly property bool chosen: root.scripture.label === modelData.label
                                width: verseColumn.width
                                height: hitText.implicitHeight + 20
                                radius: 8
                                color: chosen ? Theme.rowSelected : hitMouse.containsMouse ? Theme.raised : "transparent"
                                border.color: chosen ? Theme.next : "transparent"
                                LText {
                                    id: hitText
                                    x: 12
                                    y: 10
                                    width: parent.width - 24
                                    text: "<b>" + root.escaped(hitRow.modelData.label) + "</b>  " + root.escaped(hitRow.modelData.text)
                                    textFormat: Text.StyledText
                                    wrapMode: Text.WordWrap
                                    elide: Text.ElideNone
                                    maximumLineCount: 3
                                    lineHeight: 1.3
                                }
                                MouseArea {
                                    id: hitMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    // Into the reference: the passage shows,
                                    // ready for Preview or Show now.
                                    onClicked: {
                                        bookInput.text = hitRow.modelData.book
                                        chapterInput.text = String(hitRow.modelData.chapter)
                                        verseInput.text = String(hitRow.modelData.verse)
                                        root.refresh()
                                    }
                                    onDoubleClicked: root.scripture.showNow()
                                }
                            }
                        }

                        SectionLabel {
                            visible: root.scripture.wordQuery.trim() === ""
                            text: root.scripture.versesPerSlide === 1 ? qsTr("VERSES · ONE PER SLIDE")
                                  : root.scripture.versesPerSlide === 2 ? qsTr("VERSES · TWO PER SLIDE")
                                                                        : qsTr("VERSES · AS MANY AS FIT")
                        }
                        LText {
                            width: parent.width
                            visible: root.scripture.verses.length === 0 && root.scripture.wordQuery.trim() === ""
                            text: root.referenceText === "" ? qsTr("Type a book above (j for John), then the chapter and verse.")
                                  : root.suggesting ? root.hint
                                  : root.scripture.error
                            wrapMode: Text.WordWrap
                            color: Theme.textMuted
                        }
                        Repeater {
                            model: root.scripture.wordQuery.trim() === "" ? root.scripture.verses : []
                            delegate: Rectangle {
                                id: verseRow
                                required property var modelData
                                required property int index
                                objectName: "verse-" + index
                                width: verseColumn.width
                                height: verseText.implicitHeight + 24
                                radius: 8
                                // The verses on the first slide.
                                color: index < root.scripture.firstSlideVerses ? Theme.rowSelected : Theme.raised
                                border.color: index < root.scripture.firstSlideVerses ? Theme.next : "transparent"

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
                        // 16:9 when there is room; smaller so the options
                        // below always stay in view.
                        Layout.fillHeight: true
                        Layout.maximumHeight: width * 9 / 16
                        Layout.minimumHeight: 90
                        radius: 8
                        color: "#1B1F2B"
                        // Click: in Preview. Double-click: on the screen now.
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.scripture.preview()
                            onDoubleClicked: root.scripture.showNow()
                        }
                        // The first slide, drawn as the output draws it (theme,
                        // fitted size, side by side columns), shrunk to fit.
                        SlideFrame {
                            objectName: "scriptureSlidePreview"
                            anchors.centerIn: parent
                            height: Math.min(parent.height, parent.width * 9 / 16)
                            width: height * 16 / 9
                            text: root.scripture.previewSlide.text || ""
                            slideStyle: root.scripture.previewSlide.style || ({})
                            footer: root.scripture.previewSlide.footer || ""
                            elements: root.scripture.previewSlide.elements || []
                            frameWidth: 0
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
                            model: [1, 2, 0]
                            delegate: LButton {
                                required property int modelData
                                objectName: "versesPerSlide-" + modelData
                                implicitHeight: 32
                                radius: 6
                                text: modelData === 0 ? qsTr("Fit to slide") : String(modelData)
                                variant: root.scripture.versesPerSlide === modelData ? "primary" : "default"
                                onClicked: {
                                    root.scripture.versesPerSlide = modelData
                                    verseInput.forceActiveFocus()
                                }
                            }
                        }
                    }
                    RowLayout {
                        spacing: 6
                        LText {
                            text: qsTr("Reference")
                            font.pixelSize: 12
                            color: Theme.textMuted
                            rightPadding: 6
                        }
                        Repeater {
                            model: [{ value: "below", label: qsTr("Below text") },
                                    { value: "above", label: qsTr("Above text") },
                                    { value: "lowerThird", label: qsTr("Lower third") }]
                            delegate: LButton {
                                required property var modelData
                                objectName: "referencePosition-" + modelData.value
                                implicitHeight: 32
                                radius: 6
                                text: modelData.label
                                variant: root.scripture.referencePosition === modelData.value ? "primary" : "default"
                                onClicked: {
                                    root.scripture.referencePosition = modelData.value
                                    verseInput.forceActiveFocus()
                                }
                            }
                        }
                    }
                    RowLayout {
                        spacing: 6
                        LText {
                            text: qsTr("Copyright line")
                            font.pixelSize: 12
                            color: Theme.textMuted
                            rightPadding: 6
                        }
                        LButton {
                            objectName: "copyrightOn"
                            implicitHeight: 32
                            radius: 6
                            text: qsTr("Show")
                            variant: root.scripture.showCopyright ? "primary" : "default"
                            onClicked: { root.scripture.showCopyright = true; verseInput.forceActiveFocus() }
                        }
                        LButton {
                            objectName: "copyrightOff"
                            implicitHeight: 32
                            radius: 6
                            text: qsTr("Hide")
                            variant: root.scripture.showCopyright ? "default" : "primary"
                            onClicked: { root.scripture.showCopyright = false; verseInput.forceActiveFocus() }
                        }
                        LText {
                            Layout.fillWidth: true
                            visible: !root.scripture.showCopyright
                            text: qsTr("CC BY-SA Bibles keep a short credit on the first slide (their licence asks for it).")
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: Theme.textMuted
                        }
                    }
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

    // All translations, grouped by language, with a filter. A click outside
    // or Escape closes it.
    MouseArea {
        anchors.fill: parent
        visible: root.listMode !== ""
        z: 50
        onClicked: { root.listMode = ""; root.listFilter = "" }
    }
    Rectangle {
        objectName: "translationList"
        visible: root.listMode !== ""
        z: 51
        x: 20
        y: 120
        width: Math.min(520, root.width - 40)
        height: Math.min(listColumn.implicitHeight + 16, root.height - y - 20)
        radius: 8
        color: Theme.window
        border.color: Theme.border
        onVisibleChanged: if (visible) listFilterField.focusInput()
        ColumnLayout {
            id: listColumn
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6
            SearchField {
                id: listFilterField
                objectName: "translationFilter"
                inputName: "translationFilterText"
                Layout.fillWidth: true
                placeholder: root.listMode === "second" ? qsTr("Side by side with… (type to filter)")
                                                         : qsTr("Type a language or name to filter")
                text: root.listFilter
                onTextChanged: root.listFilter = text
                onAccepted: {
                    const rows = root.listRows().filter(r => !r.heading)
                    if (rows.length > 0)
                        root.pickFromList(rows[0].index)
                }
            }
            ListView {
                id: translationListView
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 420)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.listMode !== "" ? root.listRows() : []
                delegate: Rectangle {
                    id: listRow
                    required property var modelData
                    objectName: modelData.heading ? "translationHeading-" + modelData.language : "translationItem-" + modelData.id
                    width: translationListView.width
                    height: modelData.heading ? 26 : 34
                    radius: 6
                    color: !modelData.heading && rowMouse.containsMouse ? Theme.raised : "transparent"
                    readonly property bool chosen: !modelData.heading
                                                   && modelData.index === (root.listMode === "second" ? root.scripture.secondTranslation
                                                                                                      : root.scripture.translation)
                    SectionLabel {
                        visible: listRow.modelData.heading
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 4
                        text: listRow.modelData.language.toUpperCase()
                    }
                    RowLayout {
                        visible: !listRow.modelData.heading
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 8
                        spacing: 10
                        LText {
                            Layout.preferredWidth: 110
                            text: listRow.modelData.abbreviation
                            font.weight: listRow.chosen ? Font.Bold : Font.DemiBold
                            color: listRow.chosen ? "#BFD3FF" : Theme.text
                        }
                        LText {
                            Layout.fillWidth: true
                            text: listRow.modelData.title
                            font.pixelSize: 12
                            color: Theme.textMuted
                        }
                    }
                    MouseArea {
                        id: rowMouse
                        anchors.fill: parent
                        enabled: !listRow.modelData.heading
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.pickFromList(listRow.modelData.index)
                    }
                }
            }
        }
        Keys.onEscapePressed: { root.listMode = ""; root.listFilter = "" }
    }

    Loader {
        id: folderDialog
        active: false
        sourceComponent: FolderDialog {
            title: qsTr("Add a Bible from a folder (USFM files, or OpenLP books.csv and verses.csv)")
            Component.onCompleted: open()
            onAccepted: {
                root.scripture.addBible(selectedFolder)
                folderDialog.active = false
            }
            onRejected: folderDialog.active = false
        }
    }
    Loader {
        id: fileDialog
        active: false
        sourceComponent: FileDialog {
            title: qsTr("Add a Bible from a file")
            nameFilters: [qsTr("Bibles (*.xml *.xmm *.usfx *.csv)"), qsTr("All files (*)")]
            Component.onCompleted: open()
            onAccepted: {
                root.scripture.addBible(selectedFile)
                fileDialog.active = false
            }
            onRejected: fileDialog.active = false
        }
    }
}
