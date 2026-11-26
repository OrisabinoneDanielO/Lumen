import QtQuick
import QtQuick.Layouts
import QtQuick.Window

// Main operator window, laid out as design/mockups/Main.html. Everything it
// shows and does goes through the OperatorController.
Window {
    id: window

    required property QtObject controller

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

        // Live shortcuts, unless the operator is typing in the search box.
        Keys.onPressed: event => {
            if (library.searchHasFocus)
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
            }
        }
    }
}
