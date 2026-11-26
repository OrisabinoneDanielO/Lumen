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
                onPropsRequested: {
                    timersPopover.active = false
                    messagePopover.visible = false
                    stagePopover.active = false
                    propsPopover.active = window.propsController !== null && !propsPopover.active
                }
                onStageRequested: {
                    propsPopover.active = false
                    timersPopover.active = false
                    messagePopover.visible = false
                    stagePopover.active = window.stageController !== null && !stagePopover.active
                }
                onTimersRequested: {
                    propsPopover.active = false
                    messagePopover.visible = false
                    stagePopover.active = false
                    timersPopover.active = window.timerController !== null && !timersPopover.active
                }
                onMessageRequested: {
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
                }

                SlideGridPanel {
                    objectName: "slideGrid"
                    visible: window.controller.mode === "show"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    controller: window.controller
                    onSlideClicked: keyRoot.forceActiveFocus()
                }

                EditorPanel {
                    objectName: "editor"
                    visible: window.controller.mode === "edit"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    controller: window.controller
                }

                PreviewLivePanel {
                    objectName: "previewLive"
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
