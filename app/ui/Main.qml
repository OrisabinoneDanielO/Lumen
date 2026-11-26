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

    width: 1440
    height: 900
    minimumWidth: 1024
    minimumHeight: 640
    visible: true
    title: qsTr("Lumen Presenter")
    color: Theme.window

    FocusScope {
        id: keyRoot
        objectName: "keyRoot"
        anchors.fill: parent
        focus: true

        // Live shortcuts, unless the operator is typing in a search box.
        Keys.onPressed: event => {
            if (library.searchHasFocus || (scripturePanel.item && scripturePanel.item.visible)
                || (themePanel.item && themePanel.item.visible))
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
                    objectName: "servicePlan"
                    Layout.preferredWidth: Theme.planWidth
                    Layout.fillHeight: true
                    controller: window.controller
                    onItemClicked: keyRoot.forceActiveFocus()
                    onShareRequested: sharePopover.active = window.sharing !== null
                    onRemoteRequested: remotePopover.active = window.remote !== null
                    onScreensRequested: if (window.screenSetup) window.screenSetup.openSetup()
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

                // Edit mode has the editor's own panels instead (as in
                // design/mockups/Editor.html).
                PreviewLivePanel {
                    objectName: "previewLive"
                    visible: window.controller.mode !== "edit"
                    Layout.preferredWidth: Theme.previewWidth
                    Layout.fillHeight: true
                    controller: window.controller
                }
            }

            LibraryPanel {
                id: library
                objectName: "library"
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.libraryHeight
                controller: window.controller
                onSearchDone: keyRoot.forceActiveFocus()
                onThemesRequested: {
                    if (themePanel.item)
                        themePanel.item.show()
                }
            }
        }
    }

    Loader {
        id: scripturePanel
        anchors.fill: parent
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
        sourceComponent: ScreensPopover {
            setup: window.screenSetup
            onCloseRequested: window.screenSetup.close()
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
            flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
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
        id: remotePopover
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
}
