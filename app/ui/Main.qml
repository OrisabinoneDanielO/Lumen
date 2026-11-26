import QtQuick
import QtQuick.Layouts
import QtQuick.Window

// Main operator window, laid out as design/mockups/Main.html. Everything it
// shows and does goes through the OperatorController.
Window {
    id: window

    required property QtObject controller
    // Scripture quick search; optional (the panel is left out without it).
    property QtObject scripture: null
    // The theme editor; optional too.
    property QtObject themes: null
    // Timers (batch 12); optional.
    property QtObject timerController: null
    // The stage display (batch 13); optional.
    property QtObject stageController: null
    // Props and message templates (batch 14); optional.
    property QtObject propsController: null
    property QtObject messageTemplates: null
    // The stream output (batch 15); optional.
    property QtObject streamController: null
    // The slide editor (batch 16); optional.
    property QtObject editorController: null
    // Sharing on the local network (batch 18); optional.
    property QtObject sharing: null
    // Remote control (batch 19); optional.
    property QtObject remote: null
    property QtObject osc: null
    property QtObject midi: null
    // Timecode cues (batch 20); optional.
    property QtObject cues: null
    // The Screens setup (which screen is the main output, which the stage).
    property QtObject screenSetup: null
    // Optional update checks.
    property QtObject updates: null

    width: 1440
    height: 900
    minimumWidth: 1024
    minimumHeight: 640

    // Panel sizes follow the window; dragging a border between panels sets
    // the share it takes (double-click a border for the default).
    property real planShare: 0.19
    property real previewShare: 0.25
    property real libraryShare: 0.26
    readonly property int planWidth: Math.round(Math.max(200, Math.min(width * 0.32, width * planShare)))
    readonly property int previewWidth: Math.round(Math.max(280, Math.min(width * 0.45, width * previewShare)))
    readonly property int libraryHeight: Math.round(Math.max(130, Math.min(height * 0.5, height * libraryShare)))
    visible: true
    title: qsTr("Lumen Presenter")
    color: Theme.window

    // The safety keys work from anywhere in Lumen: a search box, a popover,
    // the scripture window, the Screens setup (F1-F5 clear, F6 Logo, F7 Black).
    component LiveKey: Shortcut {
        required property int key
        context: Qt.ApplicationShortcut
        enabled: !(themePanel.item && themePanel.item.visible)
        onActivated: window.controller.handleKey(key, Qt.NoModifier)
    }
    LiveKey { sequence: "F1"; key: Qt.Key_F1 }
    LiveKey { sequence: "F2"; key: Qt.Key_F2 }
    LiveKey { sequence: "F3"; key: Qt.Key_F3 }
    LiveKey { sequence: "F4"; key: Qt.Key_F4 }
    LiveKey { sequence: "F5"; key: Qt.Key_F5 }
    LiveKey { sequence: "F6"; key: Qt.Key_F6 }
    LiveKey { sequence: "F7"; key: Qt.Key_F7 }

    // Escape closes whichever popover is open (true if one was).
    function closePopovers() {
        let closed = false
        for (const loader of [timersPopover, cuesPopover, updatePopover, remotePopover, sharePopover,
                              streamPopover, propsPopover, stagePopover]) {
            if (loader.active) {
                loader.active = false
                closed = true
            }
        }
        if (messagePopover.visible) {
            messagePopover.visible = false
            closed = true
        }
        if (outputConfirm.visible) {
            outputConfirm.visible = false
            closed = true
        }
        if (planPanel.closeMenus())
            closed = true
        if (previewLivePanel.closeMenus())
            closed = true
        return closed
    }

    FocusScope {
        id: keyRoot
        objectName: "keyRoot"
        anchors.fill: parent
        focus: true

        // Live shortcuts, unless the operator is typing in a search box or
        // setting up the screens (there Escape and Return close the setup).
        Keys.onPressed: event => {
            if (window.screenSetup && window.screenSetup.open) {
                if (event.key === Qt.Key_Escape || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                    window.screenSetup.close()
                event.accepted = true
                return;
            }
            if (event.key === Qt.Key_Escape && window.closePopovers()) {
                event.accepted = true
                return;
            }
            if (library.searchHasFocus || (themePanel.item && themePanel.item.visible))
                return;
            event.accepted = window.controller.handleKey(event.key, event.modifiers);
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Toolbar {
                objectName: "toolbar"
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                controller: window.controller
                timerSummary: window.timerController ? window.timerController.summary : ""
                stageConnected: window.stageController ? window.stageController.connected : false
                streamConnected: window.streamController ? window.streamController.connected : false
                onOutputOffRequested: outputConfirm.visible = true
                onStreamRequested: {
                    timersPopover.active = false
                    messagePopover.visible = false
                    stagePopover.active = false
                    propsPopover.active = false
                    streamPopover.active = window.streamController !== null && !streamPopover.active
                }
                onPropsRequested: {
                    streamPopover.active = false
                    timersPopover.active = false
                    messagePopover.visible = false
                    stagePopover.active = false
                    propsPopover.active = window.propsController !== null && !propsPopover.active
                }
                onStageRequested: {
                    streamPopover.active = false
                    propsPopover.active = false
                    timersPopover.active = false
                    messagePopover.visible = false
                    stagePopover.active = window.stageController !== null && !stagePopover.active
                }
                onTimersRequested: {
                    streamPopover.active = false
                    propsPopover.active = false
                    messagePopover.visible = false
                    stagePopover.active = false
                    timersPopover.active = window.timerController !== null && !timersPopover.active
                }
                onMessageRequested: {
                    streamPopover.active = false
                    propsPopover.active = false
                    timersPopover.active = false
                    stagePopover.active = false
                    messagePopover.visible = !messagePopover.visible
                    if (messagePopover.visible)
                        messagePopover.focusInput()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                ServicePlanPanel {
                    id: planPanel
                    objectName: "servicePlan"
                    Layout.preferredWidth: window.planWidth
                    Layout.fillHeight: true
                    controller: window.controller
                    onItemClicked: keyRoot.forceActiveFocus()
                    onShareRequested: sharePopover.active = window.sharing !== null
                    onRemoteRequested: remotePopover.active = window.remote !== null
                    onScreensRequested: if (window.screenSetup) window.screenSetup.openSetup()
                    onUpdatesRequested: updatePopover.active = window.updates !== null
                    updateVersion: window.updates && window.updates.available ? window.updates.latestVersion : ""
                    dragGhost: dragGhost
                    onAddRequested: what => {
                        if (what === "scripture")
                            window.controller.scriptureRequested()
                        else
                            library.showPage(what === "media" ? 2 : what === "decks" ? 3 : 0)
                    }
                }
                DragHandle {
                    objectName: "planHandle"
                    Layout.fillHeight: true
                    onMoved: delta => window.planShare = Math.max(0.12, Math.min(0.32, window.planShare + delta / window.width))
                    onReset: window.planShare = 0.19
                }

                SlideGridPanel {
                    objectName: "slideGrid"
                    visible: window.controller.mode === "show"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    controller: window.controller
                    cuesAvailable: window.cues !== null && window.cues.available
                    onSlideClicked: keyRoot.forceActiveFocus()
                    onCuesRequested: cuesPopover.active = !cuesPopover.active
                }

                EditorPanel {
                    objectName: "editor"
                    visible: window.controller.mode === "edit"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    controller: window.controller
                    editor: window.editorController
                    timers: window.timerController
                }

                DragHandle {
                    objectName: "previewHandle"
                    visible: window.controller.mode !== "edit"
                    Layout.fillHeight: true
                    onMoved: delta => window.previewShare = Math.max(0.18, Math.min(0.45, window.previewShare - delta / window.width))
                    onReset: window.previewShare = 0.25
                }
                // Edit mode has the editor's own panels instead (as in
                // design/mockups/Editor.html).
                PreviewLivePanel {
                    id: previewLivePanel
                    objectName: "previewLive"
                    visible: window.controller.mode !== "edit"
                    Layout.preferredWidth: window.previewWidth
                    placementWarning: window.screenSetup ? window.screenSetup.placementWarning : ""
                    onMessageRequested: {
                        messagePopover.visible = true
                        messagePopover.focusInput()
                    }
                    Layout.fillHeight: true
                    controller: window.controller
                }
            }

            DragHandle {
                objectName: "libraryHandle"
                horizontal: true
                Layout.fillWidth: true
                onMoved: delta => window.libraryShare = Math.max(0.15, Math.min(0.5, window.libraryShare - delta / window.height))
                onReset: window.libraryShare = 0.26
            }
            LibraryPanel {
                id: library
                objectName: "library"
                Layout.fillWidth: true
                Layout.preferredHeight: window.libraryHeight
                controller: window.controller
                dragGhost: dragGhost
                onSearchDone: keyRoot.forceActiveFocus()
                // A row or card clicked: the keyboard goes back to the show.
                onPicked: keyRoot.forceActiveFocus()
                onThemesRequested: {
                    if (themePanel.item)
                        themePanel.item.show()
                }
            }
        }
    }

    // Its own window (not over this one), so the operator window stays usable.
    Loader {
        id: scripturePanel
        active: window.scripture !== null
        sourceComponent: ScripturePanel {
            scripture: window.scripture
            onClosed: keyRoot.forceActiveFocus()
        }
    }

    Loader {
        id: themePanel
        anchors.fill: parent
        active: window.themes !== null
        sourceComponent: ThemeEditorPanel {
            themes: window.themes
            onClosed: keyRoot.forceActiveFocus()
        }
    }

    Connections {
        target: window.controller
        function onScriptureRequested() {
            if (scripturePanel.item)
                scripturePanel.item.show()
        }
    }

    // Toolbar popovers (batch 12), under the toolbar on the right.
    Loader {
        id: timersPopover
        onActiveChanged: if (!active) keyRoot.forceActiveFocus()
        objectName: "timersPopover"
        active: false
        x: window.width - width - 16
        y: 62
        z: 50
        sourceComponent: TimersPopover {
            timers: window.timerController
            controller: window.controller
            onCloseRequested: timersPopover.active = false
        }
    }
    // Cues stay open while recording, beside the slides (not over them).
    Loader {
        id: cuesPopover
        onActiveChanged: if (!active) keyRoot.forceActiveFocus()
        objectName: "cuesPopover"
        active: false
        x: window.width - width - 16
        y: window.height - height - 16
        z: 50
        sourceComponent: CuesPopover {
            cues: window.cues
            onCloseRequested: cuesPopover.active = false
        }
    }
    // Turning the audience output off takes a second click: the projector
    // then shows the desktop.
    Rectangle {
        id: outputConfirm
        objectName: "outputConfirm"
        visible: false
        z: 70
        x: window.width - width - 16
        y: 62
        width: 340
        height: confirmColumn.implicitHeight + 24
        radius: 10
        color: Theme.window
        border.color: Theme.warnBorder
        onVisibleChanged: if (!visible) keyRoot.forceActiveFocus()
        Column {
            id: confirmColumn
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10
            LText {
                width: parent.width
                text: qsTr("Turn the audience output off? The screen goes to the computer's desktop until you turn it on again.")
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
                font.pixelSize: 12
            }
            Row {
                spacing: 6
                LButton {
                    objectName: "outputOffConfirm"
                    text: qsTr("Turn off")
                    variant: "warn"
                    implicitHeight: 30
                    radius: 6
                    onClicked: {
                        outputConfirm.visible = false
                        window.controller.toggleOutput()
                    }
                }
                LButton {
                    objectName: "outputOffCancel"
                    text: qsTr("Keep it on")
                    implicitHeight: 30
                    radius: 6
                    onClicked: outputConfirm.visible = false
                }
            }
        }
    }

    // The Screens setup, in the middle of the window: it opens by itself the
    // first time a set of screens is connected.
    // Behind the Screens setup: the window dims and ignores clicks.
    Rectangle {
        anchors.fill: parent
        z: 59
        visible: screensPopover.active
        color: "#A6000000"
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: wheel => wheel.accepted = true
        }
    }
    Loader {
        id: screensPopover
        objectName: "screensPopover"
        active: window.screenSetup !== null && window.screenSetup.open
        anchors.centerIn: parent
        z: 60
        onLoaded: window.requestActivate()
        sourceComponent: ScreensPopover {
            setup: window.screenSetup
            onCloseRequested: {
                window.screenSetup.close()
                keyRoot.forceActiveFocus()
            }
        }
    }
    // While it is open, the screens with no output on them show their number
    // here (the renderer draws it on the screens it is on).
    Instantiator {
        model: window.screenSetup ? window.screenSetup.screens : null
        delegate: Window {
            required property int index
            required property int number
            required property string name
            required property string role
            required property bool labelHere
            objectName: "screenLabel-" + number
            title: qsTr("Lumen screen %1").arg(number)
            screen: Qt.application.screens[index]
            // Inside its screen too: some platforms go by position.
            x: screen ? screen.virtualX : 0
            y: screen ? screen.virtualY : 0
            width: screen ? screen.width : 640
            height: screen ? screen.height : 480
            // Labels only: they never take the keyboard from the operator.
            flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
            visibility: labelHere ? Window.FullScreen : Window.Hidden
            color: Theme.window
            Column {
                anchors.centerIn: parent
                width: parent.width * 0.8
                spacing: parent.height * 0.02
                LText {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: number
                    color: Theme.live
                    font.weight: Font.Bold
                    font.pixelSize: Math.max(48, parent.parent.height * 0.4)
                }
                LText {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: name
                    font.pixelSize: Math.max(16, parent.parent.height * 0.05)
                }
                LText {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: role === "main" ? qsTr("Main output") : role === "stage" ? qsTr("Stage display") : qsTr("Not used")
                    color: Theme.textMuted
                    font.pixelSize: Math.max(14, parent.parent.height * 0.045)
                }
            }
        }
    }
    Loader {
        id: updatePopover
        onActiveChanged: if (!active) keyRoot.forceActiveFocus()
        objectName: "updatePopover"
        active: false
        x: 16
        y: 62
        z: 50
        sourceComponent: UpdatePopover {
            updates: window.updates
            onCloseRequested: updatePopover.active = false
        }
    }
    Loader {
        id: remotePopover
        onActiveChanged: if (!active) keyRoot.forceActiveFocus()
        objectName: "remotePopover"
        active: false
        x: 16
        y: 62
        z: 50
        sourceComponent: RemotePopover {
            remote: window.remote
            osc: window.osc
            midi: window.midi
            onCloseRequested: remotePopover.active = false
        }
    }
    Loader {
        id: sharePopover
        onActiveChanged: if (!active) keyRoot.forceActiveFocus()
        objectName: "sharePopover"
        active: false
        x: 16
        y: 62
        z: 50
        sourceComponent: SharePopover {
            sharing: window.sharing
            controller: window.controller
            onCloseRequested: sharePopover.active = false
        }
    }
    Loader {
        id: streamPopover
        onActiveChanged: if (!active) keyRoot.forceActiveFocus()
        objectName: "streamPopover"
        active: false
        x: window.width - width - 16
        y: 62
        z: 50
        sourceComponent: StreamPopover {
            stream: window.streamController
            onCloseRequested: streamPopover.active = false
        }
    }
    Loader {
        id: propsPopover
        onActiveChanged: if (!active) keyRoot.forceActiveFocus()
        objectName: "propsPopover"
        active: false
        x: window.width - width - 16
        y: 62
        z: 50
        sourceComponent: PropsPopover {
            props: window.propsController
            onCloseRequested: propsPopover.active = false
        }
    }
    Loader {
        id: stagePopover
        onActiveChanged: if (!active) keyRoot.forceActiveFocus()
        objectName: "stagePopover"
        active: false
        x: window.width - width - 16
        y: 62
        z: 50
        sourceComponent: StagePopover {
            stage: window.stageController
            onCloseRequested: stagePopover.active = false
        }
    }
    MessagePopover {
        id: messagePopover
        onVisibleChanged: if (!visible) keyRoot.forceActiveFocus()
        objectName: "messagePopover"
        visible: false
        x: window.width - width - 16
        y: 62
        z: 50
        controller: window.controller
        templates: window.messageTemplates
        onCloseRequested: {
            visible = false
            keyRoot.forceActiveFocus()
        }
    }

    // Follows the pointer while something is dragged onto the service plan.
    DragGhost {
        id: dragGhost
        objectName: "dragGhost"
    }
}
