import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts

// Left column: the ordered items of today's service. Clicking an item shows
// its slides; it does not send anything live. The plan menu ("Plan") makes,
// opens, duplicates, exports and imports plans, adds headers, and makes or
// restores backups; right-clicking an item moves it, edits its notes or
// removes it. Every change is saved at once.
Rectangle {
    id: root

    required property QtObject controller
    signal itemClicked(int index)
    signal shareRequested() // batch 18: the Share popover
    signal remoteRequested() // batch 19: the Remote control popover
    signal screensRequested() // the Screens setup

    // The plan menu's page: "" (closed), "main", "open", "restore", or
    // "input" (a name, header or notes, for inputAction).
    property string menuPage: ""
    property string inputAction: ""
    property int inputItem: -1
    // The item whose menu is open (right-click), or -1.
    property int itemMenuFor: -1

    function openInput(action, prompt, text, item) {
        inputAction = action
        inputItem = item === undefined ? -1 : item
        inputLabel.text = prompt
        input.text = text
        menuPage = "input"
        input.focusInput()
    }
    function acceptInput() {
        const text = input.text
        if (inputAction === "new")
            controller.newPlan(text)
        else if (inputAction === "rename")
            controller.renamePlan(text)
        else if (inputAction === "header")
            controller.addHeader(text)
        else if (inputAction === "notes")
            controller.setItemNotes(inputItem, text)
        menuPage = ""
    }

    // One line of a menu.
    component MenuEntry: Rectangle {
        id: entry
        property string text
        property string detail
        property bool danger: false
        signal triggered()
        width: parent ? parent.width : 0
        height: 32
        radius: 6
        color: entryMouse.containsMouse ? Theme.raised : "transparent"
        Accessible.role: Accessible.MenuItem
        Accessible.name: text
        LText {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.right: entryDetail.left
            anchors.verticalCenter: parent.verticalCenter
            text: entry.text
            font.pixelSize: 12
            color: entry.danger ? Theme.live : Theme.text
        }
        LText {
            id: entryDetail
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: entry.detail
            font.pixelSize: 11
            color: Theme.textMuted
        }
        MouseArea {
            id: entryMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: entry.triggered()
        }
    }

    color: Theme.panel

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: 1
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 14
            Layout.bottomMargin: 4

            SectionLabel {
                Layout.fillWidth: true
                text: qsTr("SERVICE PLAN")
            }
            LButton {
                id: menuButton
                objectName: "planMenuButton"
                text: qsTr("Plan ▾")
                variant: "ghost"
                implicitHeight: 32
                radius: 6
                onClicked: root.menuPage = root.menuPage === "" ? "main" : ""
            }
        }
        // The result of the last plan action (the plan's name is in the
        // toolbar).
        LText {
            objectName: "planMessage"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 6
            visible: text !== ""
            text: root.controller.planMessage
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            font.pixelSize: 11
            color: Theme.textMuted
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: planColumn.height + 12
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: planColumn
                x: 8
                width: parent.width - 16
                spacing: 2

                Repeater {
                    model: root.controller.plan

                    delegate: Rectangle {
                        id: planRow
                        required property int index
                        required property string kind
                        required property string title
                        required property string subtitle
                        required property string notes
                        required property bool header
                        readonly property bool current: index === root.controller.currentItem
                        readonly property bool live: index === root.controller.liveItem

                        objectName: "planItem-" + index
                        width: planColumn.width
                        height: header ? 34 : notes !== "" ? 64 : 50
                        radius: 8
                        color: current ? Theme.selected : rowMouse.containsMouse ? Theme.raised : "transparent"
                        Accessible.role: Accessible.ListItem
                        Accessible.name: header ? title : (kind ? kind + ": " : "") + title + (live ? qsTr(", live") : "")

                        // Headers: a label dividing the service ("Worship").
                        SectionLabel {
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            visible: planRow.header
                            text: planRow.title.toUpperCase()
                            color: Theme.text
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 10
                            visible: !planRow.header

                            // Orange dot: the item that is live on screen.
                            Rectangle {
                                implicitWidth: 6
                                implicitHeight: 6
                                radius: 3
                                color: planRow.live ? Theme.live : "transparent"
                            }
                            SectionLabel {
                                Layout.preferredWidth: 52
                                text: planRow.kind
                                font.pixelSize: 10
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                LText {
                                    Layout.fillWidth: true
                                    text: planRow.title
                                    font.weight: Font.Medium
                                }
                                LText {
                                    Layout.fillWidth: true
                                    visible: text !== ""
                                    text: planRow.subtitle
                                    font.pixelSize: 11
                                    color: Theme.textMuted
                                }
                                LText {
                                    objectName: "planNotes-" + planRow.index
                                    Layout.fillWidth: true
                                    visible: text !== ""
                                    text: planRow.notes
                                    font.pixelSize: 11
                                    font.italic: true
                                    color: Theme.next
                                }
                            }
                        }

                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.PointingHandCursor
                            onClicked: mouse => {
                                if (mouse.button === Qt.RightButton) {
                                    root.menuPage = ""
                                    root.itemMenuFor = planRow.index
                                    itemMenu.y = Math.min(planRow.mapToItem(root, 0, planRow.height).y,
                                                          root.height - itemMenu.height - 8)
                                    return
                                }
                                root.itemMenuFor = -1
                                root.controller.selectItem(planRow.index)
                                root.itemClicked(planRow.index)
                            }
                        }
                    }
                }
            }
        }
    }

    LText {
        anchors.centerIn: parent
        width: parent.width - 48
        visible: root.controller.plan.count === 0
        text: qsTr("No items yet. Pick a song in the library below and choose Add to plan.")
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        color: Theme.textMuted
    }

    // Clicking outside a menu closes it.
    MouseArea {
        anchors.fill: parent
        visible: root.menuPage !== "" || root.itemMenuFor >= 0
        z: 9
        onClicked: {
            root.menuPage = ""
            root.itemMenuFor = -1
        }
    }

    // The plan menu.
    Rectangle {
        id: planMenu
        objectName: "planMenu"
        visible: root.menuPage !== ""
        z: 10
        anchors.right: parent.right
        anchors.rightMargin: 12
        y: 50
        width: Math.min(280, root.width - 24)
        height: Math.min(menuColumn.implicitHeight + 12, root.height - y - 12)
        radius: 8
        color: Theme.window
        border.color: Theme.border
        clip: true

        Flickable {
            anchors.fill: parent
            anchors.margins: 6
            contentHeight: menuColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: menuColumn
                width: parent.width
                spacing: 0

                // Main page.
                Column {
                    width: parent.width
                    visible: root.menuPage === "main"
                    MenuEntry {
                        objectName: "planNew"
                        text: qsTr("New plan…")
                        onTriggered: root.openInput("new", qsTr("Name of the new plan"), qsTr("Sunday Service"))
                    }
                    MenuEntry {
                        objectName: "planOpen"
                        text: qsTr("Open plan")
                        detail: "▸"
                        onTriggered: {
                            root.controller.refreshSavedPlans()
                            root.menuPage = "open"
                        }
                    }
                    MenuEntry {
                        objectName: "planRename"
                        text: qsTr("Rename plan…")
                        onTriggered: root.openInput("rename", qsTr("Plan name"), root.controller.planName)
                    }
                    MenuEntry {
                        objectName: "planDuplicate"
                        text: qsTr("Duplicate plan")
                        onTriggered: {
                            root.controller.duplicatePlan()
                            root.menuPage = ""
                        }
                    }
                    MenuEntry {
                        objectName: "planAddHeader"
                        text: qsTr("Add header…")
                        onTriggered: root.openInput("header", qsTr("Header (e.g. Worship, Sermon)"), "")
                    }
                    MenuEntry {
                        objectName: "planExport"
                        text: qsTr("Export plan…")
                        detail: ".lumen"
                        onTriggered: {
                            root.menuPage = ""
                            exportDialog.active = true
                        }
                    }
                    MenuEntry {
                        objectName: "planImport"
                        text: qsTr("Import plan…")
                        detail: ".lumen"
                        onTriggered: {
                            root.menuPage = ""
                            importDialog.active = true
                        }
                    }
                    MenuEntry {
                        objectName: "planShare"
                        text: qsTr("Share with other computers…")
                        onTriggered: {
                            root.menuPage = ""
                            root.shareRequested()
                        }
                    }
                    MenuEntry {
                        objectName: "planScreens"
                        text: qsTr("Screens…")
                        onTriggered: {
                            root.menuPage = ""
                            root.screensRequested()
                        }
                    }
                    MenuEntry {
                        objectName: "planRemote"
                        text: qsTr("Remote control and MIDI…")
                        onTriggered: {
                            root.menuPage = ""
                            root.remoteRequested()
                        }
                    }
                    MenuEntry {
                        objectName: "planBackUp"
                        visible: root.controller.backupsAvailable
                        text: qsTr("Back up now")
                        onTriggered: {
                            root.controller.backUpNow()
                            root.menuPage = ""
                        }
                    }
                    MenuEntry {
                        objectName: "planRestore"
                        visible: root.controller.backupsAvailable
                        text: qsTr("Restore a backup")
                        detail: "▸"
                        onTriggered: root.menuPage = "restore"
                    }
                    LText {
                        x: 10
                        visible: root.controller.backupsAvailable
                        height: 26
                        verticalAlignment: Text.AlignVCenter
                        text: root.controller.lastBackup
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                }

                // Saved plans, newest first.
                Column {
                    width: parent.width
                    visible: root.menuPage === "open"
                    MenuEntry {
                        text: qsTr("◂ Back")
                        onTriggered: root.menuPage = "main"
                    }
                    Repeater {
                        model: root.controller.savedPlans
                        delegate: MenuEntry {
                            required property int index
                            required property string name
                            required property string date
                            required property bool open
                            objectName: "savedPlan-" + index
                            text: (open ? "● " : "") + name
                            detail: date
                            onTriggered: {
                                // Close first: opening rebuilds this list.
                                const row = index
                                const panel = root
                                panel.menuPage = ""
                                panel.controller.openPlan(row)
                            }
                        }
                    }
                }

                // Backups, newest first. Restoring restarts Lumen.
                Column {
                    width: parent.width
                    visible: root.menuPage === "restore"
                    MenuEntry {
                        text: qsTr("◂ Back")
                        onTriggered: root.menuPage = "main"
                    }
                    LText {
                        x: 10
                        width: parent.width - 20
                        text: qsTr("Restoring replaces the library with the backup and restarts Lumen. The current library is kept beside it.")
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                    Repeater {
                        model: root.controller.backups
                        delegate: MenuEntry {
                            required property int index
                            required property string made
                            required property string size
                            objectName: "backup-" + index
                            text: made
                            detail: size
                            danger: true
                            onTriggered: root.controller.restoreBackup(index)
                        }
                    }
                }

                // A name, header or notes.
                Column {
                    width: parent.width
                    visible: root.menuPage === "input"
                    spacing: 6
                    LText {
                        id: inputLabel
                        x: 4
                        font.pixelSize: 12
                    }
                    SearchField {
                        id: input
                        objectName: "planInput"
                        inputName: "planInputText"
                        width: parent.width
                        onAccepted: root.acceptInput()
                        onEscaped: root.menuPage = ""
                    }
                    RowLayout {
                        width: parent.width
                        Item { Layout.fillWidth: true }
                        LButton {
                            text: qsTr("Cancel")
                            implicitHeight: 30
                            radius: 6
                            onClicked: root.menuPage = ""
                        }
                        LButton {
                            objectName: "planInputOk"
                            text: qsTr("OK")
                            variant: "primary"
                            implicitHeight: 30
                            radius: 6
                            onClicked: root.acceptInput()
                        }
                    }
                }
            }
        }
    }

    // An item's menu (right-click).
    Rectangle {
        id: itemMenu
        objectName: "itemMenu"
        visible: root.itemMenuFor >= 0
        z: 10
        x: 24
        width: Math.min(220, root.width - 48)
        height: itemColumn.implicitHeight + 12
        radius: 8
        color: Theme.window
        border.color: Theme.border

        Column {
            id: itemColumn
            anchors.fill: parent
            anchors.margins: 6
            MenuEntry {
                objectName: "itemMoveUp"
                text: qsTr("Move up")
                visible: root.itemMenuFor > 0
                onTriggered: {
                    root.controller.moveItem(root.itemMenuFor, root.itemMenuFor - 1)
                    root.itemMenuFor = -1
                }
            }
            MenuEntry {
                objectName: "itemMoveDown"
                text: qsTr("Move down")
                visible: root.itemMenuFor >= 0 && root.itemMenuFor < root.controller.plan.count - 1
                onTriggered: {
                    root.controller.moveItem(root.itemMenuFor, root.itemMenuFor + 1)
                    root.itemMenuFor = -1
                }
            }
            MenuEntry {
                objectName: "itemNotes"
                text: qsTr("Notes…")
                onTriggered: {
                    const item = root.itemMenuFor
                    root.itemMenuFor = -1
                    root.openInput("notes", qsTr("Notes for this item"),
                                   root.controller.plan.get(item).notes, item)
                }
            }
            MenuEntry {
                objectName: "itemRemove"
                text: qsTr("Remove from plan")
                danger: true
                onTriggered: {
                    root.controller.removeItem(root.itemMenuFor)
                    root.itemMenuFor = -1
                }
            }
        }
    }

    // Dialogs are created only when used.
    Loader {
        id: exportDialog
        active: false
        sourceComponent: FileDialog {
            title: qsTr("Export plan")
            fileMode: FileDialog.SaveFile
            defaultSuffix: "lumen"
            nameFilters: [qsTr("Lumen plans (*.lumen)")]
            Component.onCompleted: open()
            onAccepted: {
                root.controller.exportPlan(selectedFile)
                exportDialog.active = false
            }
            onRejected: exportDialog.active = false
        }
    }
    Loader {
        id: importDialog
        active: false
        sourceComponent: FileDialog {
            title: qsTr("Import plan")
            fileMode: FileDialog.OpenFile
            nameFilters: [qsTr("Lumen plans (*.lumen)"), qsTr("All files (*)")]
            Component.onCompleted: open()
            onAccepted: {
                root.controller.importPlan(selectedFile)
                importDialog.active = false
            }
            onRejected: importDialog.active = false
        }
    }
}
