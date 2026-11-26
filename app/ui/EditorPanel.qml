import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import Lumen.Slides

// Edit mode (batch 16), after design/mockups/Editor.html: the slide editor.
// It works on a draft of the selected item; nothing is stored, and nothing
// reaches the screen, until Save. Slides presentations take text boxes,
// shapes, pictures, videos, clocks and countdowns, placed on the canvas by
// dragging (the corner handle resizes) and styled on the right. Songs are
// edited as lyrics: a blank line starts a slide, [Verse 2] starts a group.
// Shown only with experimental/editor on.
Rectangle {
    id: root

    required property QtObject controller
    property QtObject editor: null
    property QtObject timers: null // for countdown elements

    readonly property bool ready: editor !== null && editor.open
    readonly property var element: ready ? editor.selected : ({})
    readonly property bool hasElement: ready && editor.element >= 0
    readonly property var style: hasElement && element.style ? element.style : ({})
    readonly property string type: hasElement ? (element.type || "") : ""
    readonly property bool textLike: type === "text" || type === "clock" || type === "timer"
    // The draft follows the item chosen (in the plan or the library). With
    // unsaved changes to another item, it asks: save, discard, or keep
    // editing; it never drops them unasked.
    readonly property string chosenId: controller.selectedPresentationId
    property bool otherChosen: false

    function openDraft() {
        if (!editor || !visible)
            return
        if (!editor.dirty) {
            editor.openSelected()
            otherChosen = false
        } else {
            otherChosen = editor.presentationId !== chosenId && chosenId !== ""
        }
    }
    onVisibleChanged: openDraft()
    onChosenIdChanged: openDraft()

    color: Theme.window
    clip: true

    Rectangle {
        objectName: "editorUnsaved"
        visible: root.otherChosen && root.editor !== null && root.editor.dirty
        z: 40
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 12
        width: Math.min(parent.width - 32, unsavedRow.implicitWidth + 32)
        height: unsavedRow.implicitHeight + 20
        radius: 10
        color: Theme.window
        border.color: Theme.warnBorder
        RowLayout {
            id: unsavedRow
            anchors.centerIn: parent
            spacing: 8
            LText {
                text: qsTr("Unsaved changes to \"%1\".").arg(root.editor ? root.editor.title : "")
                font.pixelSize: 12
            }
            LButton {
                objectName: "editorUnsavedSave"
                text: qsTr("Save, then edit the new item")
                variant: "primary"
                implicitHeight: 28
                radius: 6
                onClicked: {
                    root.editor.save()
                    root.openDraft()
                }
            }
            LButton {
                objectName: "editorUnsavedDiscard"
                text: qsTr("Discard")
                variant: "warn"
                implicitHeight: 28
                radius: 6
                onClicked: {
                    root.editor.openPresentation(root.editor.presentationId) // back as saved
                    root.openDraft()
                }
            }
            LButton {
                objectName: "editorUnsavedKeep"
                text: qsTr("Keep editing")
                implicitHeight: 28
                radius: 6
                onClicked: root.otherChosen = false
            }
        }
    }

    component Small: LButton {
        implicitHeight: 28
        radius: 6
        fontSize: 11
    }
    component Choice: LButton {
        property bool chosen: false
        implicitHeight: 28
        radius: 6
        fontSize: 11
        variant: chosen ? "primary" : "default"
    }
    component Swatches: Row {
        id: swatches
        property string current
        signal picked(string colour)
        spacing: 6
        Repeater {
            model: ["#FFFFFF", "#F2C38B", "#F08A24", "#E5484D", "#5B8DEF", "#70AD47", "#171A20", "#000000"]
            delegate: Rectangle {
                required property string modelData
                width: 24
                height: 24
                radius: 12
                color: modelData
                border.width: swatches.current.toUpperCase() === modelData ? 3 : 1
                border.color: swatches.current.toUpperCase() === modelData ? Theme.next : Theme.border
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: swatches.picked(parent.modelData)
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- top: title, state, Cancel and Save ----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: Theme.panel
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 10
                SearchField {
                    id: titleField
                    objectName: "editorTitle"
                    inputName: "editorTitleText"
                    Layout.preferredWidth: 260
                    visible: root.ready
                    text: root.ready ? root.editor.title : ""
                    placeholder: qsTr("Title")
                    onAccepted: root.editor.title = text
                }
                LText {
                    objectName: "editorState"
                    Layout.fillWidth: true
                    text: !root.ready ? qsTr("Select an item in the plan to edit it.")
                          : root.editor.message !== "" && !root.editor.dirty ? root.editor.message
                          : root.editor.dirty ? qsTr("Unsaved changes. Nothing reaches the screen until you save.")
                          : qsTr("Nothing here reaches the screen until you save.")
                    color: root.ready && root.editor.dirty ? Theme.live : Theme.textMuted
                    font.pixelSize: 12
                }
                Small {
                    objectName: "editorNew"
                    text: qsTr("New slides")
                    visible: root.editor !== null
                    onClicked: root.editor.newPresentation(qsTr("New slides"))
                }
                Small {
                    objectName: "editorCancel"
                    text: qsTr("Cancel")
                    enabled: root.ready && root.editor.dirty
                    opacity: enabled ? 1 : 0.5
                    onClicked: root.editor.cancel()
                }
                Small {
                    objectName: "editorSave"
                    text: qsTr("Save")
                    variant: "primary"
                    enabled: root.ready && (root.editor.dirty || titleField.text !== root.editor.title)
                    opacity: enabled ? 1 : 0.5
                    onClicked: {
                        if (titleField.text !== "" && titleField.text !== root.editor.title)
                            root.editor.title = titleField.text
                        root.editor.save()
                    }
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---- left: lyrics (songs) ----
            Rectangle {
                visible: root.ready && root.editor.isSong
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                color: Theme.panel
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 8
                    SectionLabel { text: qsTr("LYRICS") }
                    LText {
                        Layout.fillWidth: true
                        text: qsTr("Type or paste lyrics. A blank line starts a new slide; a line like [Verse 2] starts a new group.")
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 6
                        color: Theme.window
                        border.color: lyrics.activeFocus ? Theme.next : Theme.border
                        Flickable {
                            id: lyricsFlick
                            anchors.fill: parent
                            anchors.margins: 8
                            contentHeight: lyrics.contentHeight
                            clip: true
                            TextEdit {
                                id: lyrics
                                objectName: "editorLyrics"
                                width: lyricsFlick.width
                                text: root.ready ? root.editor.lyrics : ""
                                color: Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                wrapMode: TextEdit.Wrap
                                selectByMouse: true
                                onTextChanged: if (root.ready && activeFocus) root.editor.lyrics = text
                            }
                        }
                    }
                }
                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: Theme.border
                }
            }

            // ---- centre: insert, canvas, slides ----
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 16
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    LText {
                        text: qsTr("Insert")
                        font.pixelSize: 12
                        color: Theme.textMuted
                    }
                    Repeater {
                        model: [
                            { type: "text", label: qsTr("Text box") },
                            { type: "shape", label: qsTr("Shape") },
                            { type: "image", label: qsTr("Image") },
                            { type: "video", label: qsTr("Video") },
                            { type: "clock", label: qsTr("Clock") },
                            { type: "timer", label: qsTr("Countdown") }
                        ]
                        delegate: LButton {
                            required property var modelData
                            objectName: "editorInsert-" + modelData.type
                            text: modelData.label
                            implicitHeight: 32
                            enabled: root.ready && root.editor.canInsert
                            opacity: enabled ? 1 : 0.5
                            onClicked: {
                                if (modelData.type === "image" || modelData.type === "video") {
                                    mediaDialog.insertType = modelData.type
                                    mediaDialog.active = true
                                } else {
                                    root.editor.addElement(modelData.type)
                                }
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    LText {
                        text: "1920 × 1080"
                        font.pixelSize: 12
                        color: Theme.textMuted
                    }
                }

                // Canvas: the slide being edited, with the title-safe area.
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Rectangle {
                        id: canvas
                        objectName: "editorCanvas"
                        anchors.centerIn: parent
                        width: Math.min(parent.width, parent.height * 16 / 9)
                        height: width * 9 / 16
                        color: Theme.slideBackground
                        border.color: Theme.border
                        clip: true
                        readonly property real scale: width / 1920

                        // A click on empty canvas drops the selection.
                        MouseArea {
                            anchors.fill: parent
                            onClicked: if (root.ready) root.editor.element = -1
                        }

                        ComposedSlide {
                            anchors.fill: parent
                            visible: root.ready && root.editor.canInsert
                            elements: root.ready ? root.editor.elements : []
                            playVideo: false
                            Timer {
                                interval: 500
                                repeat: true
                                running: root.visible
                                onTriggered: parent.wallMs = Date.now()
                            }
                        }
                        // Songs and other items: their text as it reads.
                        LText {
                            anchors.fill: parent
                            anchors.margins: parent.width * 0.08
                            visible: !(root.ready && root.editor.canInsert)
                            text: root.ready && root.editor.slides.count > root.editor.slide
                                  ? root.editor.slides.get(root.editor.slide).text : ""
                            color: "#FFFFFF"
                            font.pixelSize: Math.max(12, canvas.height * 0.07)
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: parent.width * 0.05
                            color: "transparent"
                            border.color: "#40FFFFFF"
                            border.width: 1
                        }

                        // Handles: select, drag to move, corner to resize.
                        Repeater {
                            model: root.ready && root.editor.canInsert ? root.editor.elements : []
                            delegate: Item {
                                id: box
                                required property var modelData
                                required property int index
                                readonly property bool chosen: root.editor.element === index
                                objectName: "editorElement-" + index
                                x: modelData.frame[0] * canvas.scale
                                y: modelData.frame[1] * canvas.scale
                                width: modelData.frame[2] * canvas.scale
                                height: modelData.frame[3] * canvas.scale

                                Rectangle {
                                    anchors.fill: parent
                                    color: "transparent"
                                    border.width: box.chosen ? 2 : 1
                                    border.color: box.chosen ? Theme.next : "#30FFFFFF"
                                }
                                MouseArea {
                                    id: mover
                                    anchors.fill: parent
                                    cursorShape: Qt.SizeAllCursor
                                    property point start
                                    property var frame
                                    onPressed: mouse => {
                                        root.editor.element = box.index
                                        start = mapToItem(canvas, mouse.x, mouse.y)
                                        frame = box.modelData.frame
                                    }
                                    onPositionChanged: mouse => {
                                        const now = mapToItem(canvas, mouse.x, mouse.y)
                                        root.editor.setFrame(box.index, frame[0] + (now.x - start.x) / canvas.scale,
                                                             frame[1] + (now.y - start.y) / canvas.scale, frame[2], frame[3])
                                    }
                                }
                                Rectangle {
                                    objectName: "editorHandle-" + box.index
                                    visible: box.chosen
                                    width: 12
                                    height: 12
                                    radius: 2
                                    x: parent.width - 6
                                    y: parent.height - 6
                                    color: Theme.next
                                    MouseArea {
                                        anchors.fill: parent
                                        anchors.margins: -4
                                        cursorShape: Qt.SizeFDiagCursor
                                        property point start
                                        property var frame
                                        onPressed: mouse => {
                                            start = mapToItem(canvas, mouse.x, mouse.y)
                                            frame = box.modelData.frame
                                        }
                                        onPositionChanged: mouse => {
                                            const now = mapToItem(canvas, mouse.x, mouse.y)
                                            root.editor.setFrame(box.index, frame[0], frame[1],
                                                                 frame[2] + (now.x - start.x) / canvas.scale,
                                                                 frame[3] + (now.y - start.y) / canvas.scale)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Slide strip, with adding and removing slides.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Flickable {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 104
                        clip: true
                        contentWidth: strip.width
                        flickableDirection: Flickable.HorizontalFlick
                        boundsBehavior: Flickable.StopAtBounds

                        Row {
                            id: strip
                            spacing: 8

                            Repeater {
                                model: root.editor !== null ? root.editor.slides : root.controller.slides
                                delegate: Rectangle {
                                    id: card
                                    required property int index
                                    required property string group
                                    required property string text
                                    readonly property bool chosen: root.editor !== null ? index === root.editor.slide
                                                                                        : index === root.controller.selectedSlide

                                    objectName: "editorSlide-" + index
                                    width: 150
                                    height: 100
                                    radius: 8
                                    color: Theme.panel
                                    border.width: 2
                                    border.color: chosen ? Theme.next : Theme.border
                                    Accessible.role: Accessible.Button
                                    Accessible.name: qsTr("Slide %1, %2").arg(index + 1).arg(group)

                                    LText {
                                        x: 8
                                        y: 6
                                        text: card.group + " · " + (card.index + 1)
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                    }
                                    Rectangle {
                                        x: 6
                                        y: 26
                                        width: parent.width - 12
                                        height: parent.height - 32
                                        radius: 4
                                        color: Theme.slideBackground
                                        clip: true
                                        ComposedSlide {
                                            anchors.fill: parent
                                            visible: root.editor !== null && root.editor.canInsert
                                            elements: visible ? root.editor.slides.get(card.index).elements : []
                                            playVideo: false
                                        }
                                        LText {
                                            anchors.fill: parent
                                            anchors.margins: 4
                                            visible: !(root.editor !== null && root.editor.canInsert)
                                            text: card.text
                                            color: "#FFFFFF"
                                            font.pixelSize: 8
                                            wrapMode: Text.WordWrap
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            if (root.editor !== null)
                                                root.editor.slide = card.index
                                            root.controller.activateSlide(card.index)
                                        }
                                    }
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        visible: root.ready && root.editor.canInsert
                        spacing: 6
                        Small {
                            objectName: "editorAddSlide"
                            text: qsTr("+ Slide")
                            onClicked: root.editor.addSlide()
                        }
                        Small {
                            objectName: "editorDuplicateSlide"
                            text: qsTr("Duplicate")
                            onClicked: root.editor.duplicateSlide()
                        }
                        Small {
                            objectName: "editorRemoveSlide"
                            text: qsTr("Delete")
                            onClicked: root.editor.removeSlide()
                        }
                    }
                }
            }

            // ---- right: theme, element style, background, transition ----
            Rectangle {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                color: Theme.panel

                Rectangle {
                    width: 1
                    height: parent.height
                    color: Theme.border
                }

                Flickable {
                    anchors.fill: parent
                    anchors.leftMargin: 17
                    anchors.rightMargin: 16
                    anchors.topMargin: 16
                    contentHeight: panel.implicitHeight + 24
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: panel
                        width: parent.width
                        spacing: 10
                        enabled: root.ready
                        opacity: root.ready ? 1 : 0.5

                        SectionLabel { text: qsTr("THEME") }
                        Flow {
                            width: parent.width
                            spacing: 6
                            Choice {
                                objectName: "editorTheme-none"
                                text: qsTr("None")
                                chosen: root.ready && root.editor.theme < 0
                                onClicked: root.editor.setTheme(-1)
                            }
                            Repeater {
                                model: root.editor !== null ? root.editor.themes : null
                                delegate: Choice {
                                    required property int index
                                    required property string name
                                    objectName: "editorTheme-" + index
                                    text: name
                                    chosen: root.ready && root.editor.theme === index
                                    onClicked: root.editor.setTheme(index)
                                }
                            }
                        }

                        // The selected element.
                        SectionLabel {
                            visible: root.hasElement
                            text: root.type === "text" ? qsTr("TEXT") : root.type === "shape" ? qsTr("SHAPE")
                                : root.type === "image" ? qsTr("PICTURE") : root.type === "video" ? qsTr("VIDEO")
                                : root.type === "clock" ? qsTr("CLOCK") : qsTr("COUNTDOWN")
                        }
                        LText {
                            width: parent.width
                            visible: root.ready && root.editor.canInsert && !root.hasElement
                            text: qsTr("Click an element on the slide to style it, or insert one.")
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: Theme.textMuted
                        }
                        Rectangle {
                            visible: root.type === "text"
                            width: parent.width
                            height: 80
                            radius: 6
                            color: Theme.window
                            border.color: elementText.activeFocus ? Theme.next : Theme.border
                            TextEdit {
                                id: elementText
                                objectName: "editorText"
                                anchors.fill: parent
                                anchors.margins: 8
                                text: root.type === "text" ? root.element.text : ""
                                color: Theme.text
                                font.family: Theme.fontFamily
                                font.pixelSize: 13
                                wrapMode: TextEdit.Wrap
                                selectByMouse: true
                                onTextChanged: if (activeFocus) root.editor.setText(text)
                            }
                        }
                        Flow {
                            visible: root.textLike
                            width: parent.width
                            spacing: 6
                            Repeater {
                                model: ["Noto Sans", "Noto Serif", "Inter"]
                                delegate: Choice {
                                    required property string modelData
                                    objectName: "editorFont-" + modelData
                                    text: modelData
                                    chosen: (root.style.font || "Noto Sans") === modelData
                                    onClicked: root.editor.setStyle("font", modelData)
                                }
                            }
                        }
                        Row {
                            visible: root.textLike
                            spacing: 6
                            Small {
                                objectName: "editorSizeDown"
                                text: "−"
                                onClicked: root.editor.setStyle("size", Math.max(12, (root.style.size || 64) - 4))
                            }
                            LText {
                                objectName: "editorSize"
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("%1 px").arg(root.style.size || 64)
                                font.pixelSize: 12
                            }
                            Small {
                                objectName: "editorSizeUp"
                                text: "+"
                                onClicked: root.editor.setStyle("size", Math.min(400, (root.style.size || 64) + 4))
                            }
                        }
                        Row {
                            visible: root.textLike
                            spacing: 6
                            Repeater {
                                model: [{ v: "left", l: qsTr("Left") }, { v: "center", l: qsTr("Centre") }, { v: "right", l: qsTr("Right") }]
                                delegate: Choice {
                                    required property var modelData
                                    objectName: "editorAlign-" + modelData.v
                                    text: modelData.l
                                    chosen: (root.style.align || "center") === modelData.v
                                    onClicked: root.editor.setStyle("align", modelData.v)
                                }
                            }
                        }
                        Swatches {
                            visible: root.textLike
                            current: root.style.colour || "#FFFFFF"
                            onPicked: colour => root.editor.setStyle("colour", colour)
                        }
                        Flow {
                            visible: root.textLike
                            width: parent.width
                            spacing: 6
                            Choice {
                                objectName: "editorBold"
                                text: qsTr("Bold")
                                chosen: root.style.bold === true
                                onClicked: root.editor.setStyle("bold", !chosen)
                            }
                            Choice {
                                objectName: "editorShadow"
                                text: qsTr("Drop shadow")
                                chosen: root.style.shadow === true
                                onClicked: root.editor.setStyle("shadow", !chosen)
                            }
                            Choice {
                                objectName: "editorOutline"
                                text: qsTr("Outline")
                                chosen: (root.style.outline || 0) > 0
                                onClicked: root.editor.setStyle("outline", chosen ? 0 : 2)
                            }
                            Choice {
                                objectName: "editorFit"
                                text: qsTr("Shrink to fit")
                                chosen: root.style.fit !== false
                                onClicked: root.editor.setStyle("fit", !chosen)
                            }
                        }
                        Row {
                            visible: root.type === "clock"
                            spacing: 6
                            Choice {
                                objectName: "editorClock24"
                                text: qsTr("24-hour")
                                chosen: (root.style.format || "hh:mm") === "hh:mm"
                                onClicked: root.editor.setStyle("format", "hh:mm")
                            }
                            Choice {
                                objectName: "editorClock12"
                                text: qsTr("12-hour")
                                chosen: root.style.format === "h:mm AP"
                                onClicked: root.editor.setStyle("format", "h:mm AP")
                            }
                        }
                        Flow {
                            visible: root.type === "timer"
                            width: parent.width
                            spacing: 6
                            Repeater {
                                model: root.timers ? root.timers.timers : null
                                delegate: Choice {
                                    required property int index
                                    required property string name
                                    objectName: "editorTimer-" + index
                                    text: name
                                    chosen: root.element.timerName === name
                                    onClicked: root.editor.setTimerName(name)
                                }
                            }
                        }
                        Swatches {
                            visible: root.type === "shape"
                            current: root.style.fill || "#F08A24"
                            onPicked: colour => root.editor.setStyle("fill", colour)
                        }
                        Row {
                            visible: root.type === "shape"
                            spacing: 6
                            Choice {
                                objectName: "editorShapeRect"
                                text: qsTr("Rectangle")
                                chosen: root.style.shape !== "ellipse"
                                onClicked: root.editor.setStyle("shape", "rect")
                            }
                            Choice {
                                objectName: "editorShapeEllipse"
                                text: qsTr("Ellipse")
                                chosen: root.style.shape === "ellipse"
                                onClicked: root.editor.setStyle("shape", "ellipse")
                            }
                        }
                        LText {
                            visible: root.type === "image" || root.type === "video"
                            width: parent.width
                            text: root.hasElement ? (root.element.media || "") : ""
                            elide: Text.ElideMiddle
                            font.pixelSize: 11
                            color: Theme.textMuted
                        }
                        Row {
                            visible: root.type === "image" || root.type === "video"
                            spacing: 6
                            Choice {
                                objectName: "editorFitContain"
                                text: qsTr("Fit")
                                chosen: root.style.fit !== "cover"
                                onClicked: root.editor.setStyle("fit", "contain")
                            }
                            Choice {
                                objectName: "editorFitCover"
                                text: qsTr("Fill")
                                chosen: root.style.fit === "cover"
                                onClicked: root.editor.setStyle("fit", "cover")
                            }
                        }
                        Row {
                            visible: root.hasElement
                            spacing: 6
                            Small {
                                objectName: "editorRaise"
                                text: qsTr("Forward")
                                onClicked: root.editor.raise()
                            }
                            Small {
                                objectName: "editorLower"
                                text: qsTr("Back")
                                onClicked: root.editor.lower()
                            }
                            Small {
                                objectName: "editorDelete"
                                text: qsTr("Delete")
                                variant: "warn"
                                onClicked: root.editor.removeElement()
                            }
                        }

                        SectionLabel {
                            visible: root.ready && root.editor.canInsert
                            text: qsTr("BACKGROUND")
                        }
                        Small {
                            objectName: "editorBackground"
                            visible: root.ready && root.editor.canInsert
                            text: qsTr("Choose image or motion loop…")
                            onClicked: {
                                mediaDialog.insertType = "background"
                                mediaDialog.active = true
                            }
                        }

                        SectionLabel { text: qsTr("SLIDE TRANSITION") }
                        Flow {
                            width: parent.width
                            spacing: 6
                            Repeater {
                                model: [{ v: "theme", l: qsTr("Theme default") }, { v: "cut", l: qsTr("Cut") }, { v: "fade", l: qsTr("Dissolve") }]
                                delegate: Choice {
                                    required property var modelData
                                    objectName: "editorTransition-" + modelData.v
                                    text: modelData.l
                                    chosen: root.ready && root.editor.transition === modelData.v
                                    onClicked: root.editor.setTransition(modelData.v)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Pictures and videos for Insert and Background.
    Loader {
        id: mediaDialog
        property string insertType
        active: false
        sourceComponent: FileDialog {
            title: mediaDialog.insertType === "video" ? qsTr("Choose a video")
                 : mediaDialog.insertType === "image" ? qsTr("Choose a picture") : qsTr("Choose a picture or motion loop")
            nameFilters: mediaDialog.insertType === "image" ? [qsTr("Pictures (*.png *.jpg *.jpeg *.webp *.bmp *.gif)")]
                       : mediaDialog.insertType === "video" ? [qsTr("Videos (*.mp4 *.mov *.webm *.mkv *.m4v)")]
                       : [qsTr("Pictures and videos (*.png *.jpg *.jpeg *.webp *.mp4 *.mov *.webm *.mkv)")]
            Component.onCompleted: open()
            onAccepted: {
                if (mediaDialog.insertType === "background")
                    root.editor.setBackground(selectedFile)
                else
                    root.editor.addElement(mediaDialog.insertType, selectedFile)
                mediaDialog.active = false
            }
            onRejected: mediaDialog.active = false
        }
    }
}
